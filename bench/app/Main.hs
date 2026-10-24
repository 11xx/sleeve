{-# LANGUAGE MagicHash #-}
{-# LANGUAGE UnboxedTuples #-}
{- | The language measurement: sleeve's generator and rasterizer in C and in
Haskell over the same seeds. @check@ compares their descriptions and rasters
and shows the comparison catches a wrong one; @bench@ times each cover in each
language, the two taking turns, and reports JSON. The same source builds
natively and for WebAssembly.
-}
module Main ( main ) where

import Sleeve.Colour            ( hash, mix )
import Sleeve.Description
import Sleeve.Ffi
import Sleeve.Generate
import Sleeve.Raster

import Control.Monad            ( forM, forM_, unless, when )
import Data.Array.Byte          ( MutableByteArray(..) )
import Data.Bits                ( finiteBitSize, xor )
import Data.Char                ( ord )
import Data.Int                 ( Int64 )
import Data.List                ( intercalate, sort )
import Data.Word                ( Word64 )
import GHC.Clock                ( getMonotonicTimeNSec )
import GHC.Conc                 ( getAllocationCounter )
import GHC.Exts                 ( Int(..), RealWorld, newByteArray#, readWord8Array#, word2Int#, word8ToWord#
                                , writeWord8Array#, int2Word#, wordToWord8# )
import GHC.IO                   ( IO(..) )
import Numeric                  ( showFFloat, showHex )
import System.Environment       ( getArgs )
import System.Exit              ( exitFailure )
import System.IO                ( hFlush, hPutStrLn, stderr, stdout )
import Text.Read                ( readMaybe )


data Options = Options
  { seeds  :: !Int
  , first  :: !Word64
  , sizes  :: ![Int]
  , warmup :: !Int
  , label  :: !String
  }

usage :: String
usage = unlines
  [ "usage: sleeve-bench check [--seeds N]"
  , "       sleeve-bench bench [--seeds N] [--first SEED] [--sizes PX,PX,...] [--warmup N] [--label TEXT]"
  , "A size of 0 times generating descriptions alone." ]

parse :: Options -> [String] -> Either String Options
parse o []                      = Right o
parse o ("--seeds" : v : rest)  = number v >>= \n -> parse o { seeds = n } rest
parse o ("--first" : v : rest)  = number v >>= \n -> parse o { first = fromIntegral (n :: Integer) } rest
parse o ("--warmup" : v : rest) = number v >>= \n -> parse o { warmup = n } rest
parse o ("--label" : v : rest)  = parse o { label = v } rest
parse o ("--sizes" : v : rest)  = mapM number (splitOn ',' v) >>= \ns -> parse o { sizes = ns } rest
parse _ (other : _)             = Left ("unknown argument " <> other <> "\n" <> usage)

number :: Read a => String -> Either String a
number v = maybe (Left ("not a number: " <> v)) Right (readMaybe v)

splitOn :: Char -> String -> [String]
splitOn c s = case break (== c) s of
  (a, [])       -> [a]
  (a, _ : rest) -> a : splitOn c rest

main :: IO ()
main = do
  args <- getArgs
  let defaults = Options { seeds = 1000, first = 0, sizes = [64, 216, 512], warmup = 20, label = "default" }
  case args of
    "check" : rest -> either die' (check . orDefault 20000) (parse defaults { seeds = 0 } rest)
    "bench" : rest -> either die' bench (parse defaults rest)
    _              -> die' usage
  where
    orDefault n o = if o.seeds == 0 then o { seeds = n } else o

die' :: String -> IO a
die' message = hPutStrLn stderr message >> exitFailure

-- buffers --------------------------------------------------------------------------------------

newBuffer :: Int -> IO (MutableByteArray RealWorld)
newBuffer (I# n) = IO $ \s -> case newByteArray# n s of (# s1, m #) -> (# s1, MutableByteArray m #)

byteAt :: MutableByteArray RealWorld -> Int -> IO Int
byteAt (MutableByteArray m) (I# i) = IO $ \s -> case readWord8Array# m i s of
  (# s1, v #) -> (# s1, I# (word2Int# (word8ToWord# v)) #)

setByte :: MutableByteArray RealWorld -> Int -> Int -> IO ()
setByte (MutableByteArray m) (I# i) (I# v) = IO $ \s -> (# writeWord8Array# m i (wordToWord8# (int2Word# v)) s, () #)

-- | The largest difference between two buffers' first @n@ bytes, and how many differ.
difference :: MutableByteArray RealWorld -> MutableByteArray RealWorld -> Int -> IO (Int, Int)
difference a b n = go 0 0 0
  where
    go !i !largest !count
      | i >= n = pure (largest, count)
      | otherwise = do
          x <- byteAt a i
          y <- byteAt b i
          let d = abs (x - y)
          go (i + 1) (max largest d) (if d > 0 then count + 1 else count)

-- | Whether two descriptions are the same words; the first that differs otherwise.
sameDescription :: Description -> Description -> IO (Maybe Int)
sameDescription a b = go 0
  where
    go i
      | i >= words' = pure Nothing
      | otherwise = do
          x <- readWord a i
          y <- readWord b i
          if x /= y then pure (Just i) else go (i + 1)

-- check ----------------------------------------------------------------------------------------

check :: Options -> IO ()
check options = do
  gen     <- newGenerator
  theirs  <- newDescription
  scratch <- newScratch
  let keys = [ hash (map (fromIntegral . ord) k) | k <- ["", "sleeve", "Album Title\tArtist"] ]
      described = [0 .. fromIntegral options.seeds - 1] <> [maxBound, 4294967295] <> keys
  mismatches <- fmap concat $ forM described $ \seed -> do
    generate gen seed
    describeC seed theirs
    maybe [] (\w -> [(seed, w)]) <$> sameDescription gen.description theirs
  forM_ (take 5 mismatches) $ \(seed, w) -> hPutStrLn stderr ("description of seed " <> show seed <> " differs at word " <> show w)
  putStrLn ("descriptions: " <> show (length described) <> " seeds, " <> show (length mismatches) <> " differ")
  rasters <- forM [(64, 300), (216, 40), (512, 8)] $ \(size, count) -> do
    ours <- newBuffer (size * size * 4)
    cs   <- newBuffer (size * size * 4)
    results <- forM [0 .. count - 1] $ \seed -> do
      generate gen seed
      _ <- render scratch gen.description size size ours
      _ <- coverC seed size cs
      difference ours cs (size * size * 4)
    let largest = maximum (map fst results)
        differing = sum (map snd results)
    putStrLn ("rasters at " <> show size <> " px: " <> show count <> " covers, largest difference " <> show largest
              <> ", " <> show differing <> " of " <> show (fromIntegral count * size * size * 4) <> " bytes differ")
    pure largest
  extremes <- forM extremeCovers $ \(name, width, height, layout) -> do
    d    <- newDescription
    forM_ [0 .. words' - 1] $ \w -> writeWord d w 0
    forM_ layout $ \(w, v) -> writeWord d w v
    ours <- newBuffer (width * height * 4)
    cs   <- newBuffer (width * height * 4)
    okH  <- render scratch d width height ours
    okC  <- renderC d width height cs
    (largest, _) <- difference ours cs (width * height * 4)
    putStrLn ("extreme " <> name <> " at " <> show width <> "x" <> show height <> ": rendered " <> show (okH, okC)
              <> ", largest difference " <> show largest)
    pure (okH == 0 && okC == 0 && largest <= 1)
  -- the comparisons must catch a neighbouring seed's description and a raster two levels off in one byte
  caughtDescriptions <- fmap and $ forM [0 .. 99] $ \seed -> do
    generate gen seed
    describeC (seed + 1) theirs
    (/= Nothing) <$> sameDescription gen.description theirs
  caughtRaster <- do
    ours <- newBuffer (64 * 64 * 4)
    cs   <- newBuffer (64 * 64 * 4)
    generate gen 7
    _ <- render scratch gen.description 64 64 ours
    _ <- coverC 7 64 cs
    v <- byteAt ours 4000
    setByte ours 4000 (if v >= 2 then v - 2 else v + 2)
    (> 1) . fst <$> difference ours cs (64 * 64 * 4)
  putStrLn ("faults: a neighbouring seed's description " <> caught caughtDescriptions
            <> ", a raster two levels off in one byte " <> caught caughtRaster)
  unless (null mismatches && all (<= 1) rasters && and extremes && caughtDescriptions && caughtRaster) exitFailure
  where
    caught b = if b then "caught" else "missed"

{- | Valid covers no seed generates, at the edges of what the renderers
accept: each a name, a size, and the words it sets in a zeroed description.
-}
extremeCovers :: [(String, Int, Int, [(Int, Int)])]
extremeCovers =
  [ -- distances near 2^24 side units, where a float cannot count periods one at a time
    ( "far repeating radial", 1, 16384
    , header <> [ (kindOf l, 2), (repeatingOf l, 1), (stopCountOf l, 2), (cxOf l, 0), (cyOf l, -1000000)
                , (exOf l, 1000000), (eyOf l, 1)
                , (stopAt l 0, 0), (stopColour l 0, opaque 0x203040), (stopAt l 1, 50000), (stopColour l 1, opaque 0xe0c0a0) ] )
  ]
  where
    l = layerAt 0
    header = [(3, 1), (5, opaque 0x101010)]
    -- r | g << 8 | b << 16 with full alpha, as the word's signed 32 bits
    opaque rgb = rgb - 0x1000000

-- bench ----------------------------------------------------------------------------------------

-- | One cover's time in nanoseconds and the bytes it allocated.
data Sample = Sample
  { ns    :: !Word64
  , alloc :: !Int64
  }

window :: IO a -> IO Sample
window action = do
  a0 <- getAllocationCounter
  t0 <- getMonotonicTimeNSec
  _  <- action
  t1 <- getMonotonicTimeNSec
  a1 <- getAllocationCounter
  pure Sample { ns = t1 - t0, alloc = a0 - a1 }
{-# INLINE window #-}

data Run = Run
  { size       :: !Int
  , haskell    :: ![Sample]
  , c          :: ![Sample]
  , largest    :: !Int
  , digest     :: !Word64
  }

bench :: Options -> IO ()
bench options = do
  gen     <- newGenerator
  scratch <- newScratch
  runs <- forM options.sizes $ \size -> do
    hPutStrLn stderr ("timing " <> show options.seeds <> " covers at " <> show size <> " px")
    ours   <- newBuffer (max 1 (size * size * 4))
    cs     <- newBuffer (max 1 (size * size * 4))
    theirs <- newDescription
    let bytes = size * size * 4
        -- size 0 times generating the description alone
        h seed | size == 0 = generate gen seed >> pure 0
               | otherwise = generate gen seed >> render scratch gen.description size size ours
        k seed | size == 0 = describeC seed theirs >> pure 0
               | otherwise = coverC seed size cs
    forM_ [0 .. fromIntegral options.warmup - 1] $ \i -> h (options.first + i) >> k (options.first + i)
    samples <- forM [0 .. fromIntegral options.seeds - 1] $ \i -> do
      let seed = options.first + i
      (sh, sc) <- if even i
        then do sh <- window (h seed); sc <- window (k seed); pure (sh, sc)
        else do sc <- window (k seed); sh <- window (h seed); pure (sh, sc)
      (d, _) <- difference ours cs bytes
      digestOf <- hashBytes cs bytes
      pure (sh, sc, d, digestOf)
    pure Run
      { size    = size
      , haskell = [ s | (s, _, _, _) <- samples ]
      , c       = [ s | (_, s, _, _) <- samples ]
      , largest = maximum [ d | (_, _, d, _) <- samples ]
      , digest  = foldl (\acc (_, _, _, x) -> mix (acc `xor` x)) 0 samples
      }
  putStr (report options runs)
  hFlush stdout
  when (any ((> 1) . (.largest)) runs) $ die' "the languages' rasters differ by more than one level"

report :: Options -> [Run] -> String
report options runs = topObject
  [ ("label", str options.label)
  , ("word_bits", show (finiteBitSize (0 :: Int)))
  , ("seeds", show options.seeds)
  , ("first_seed", show options.first)
  , ("warmup", show options.warmup)
  , ("runs", list (concat [ [run r "haskell" r.haskell, run r "c" r.c] | r <- runs ]))
  , ("ratio_haskell_to_c", list
      [ object [ ("size", show r.size)
               , ("total", ratio (total r.haskell) (total r.c))
               , ("median", ratio (pct 0.5 r.haskell) (pct 0.5 r.c))
               , ("p99", ratio (pct 0.99 r.haskell) (pct 0.99 r.c)) ]
      | r <- runs ])
  , ("ratio_haskell_to_c_all_sizes", ratio (sum (map (total . (.haskell)) runs)) (sum (map (total . (.c)) runs)))
  ]
  where
    run r name samples = object
      [ ("size", show r.size), ("language", str name)
      , ("total_ms", fixed 3 (fromIntegral (total samples) / 1e6))
      , ("mean_us", fixed 3 (fromIntegral (total samples) / fromIntegral (length samples) / 1000))
      , ("median_us", micros (pct 0.5 samples)), ("p95_us", micros (pct 0.95 samples))
      , ("p99_us", micros (pct 0.99 samples)), ("max_us", micros (maximum (map (.ns) samples)))
      , ("alloc_bytes_per_cover", fixed 1 (fromIntegral (sum (map (.alloc) samples)) / fromIntegral (length samples)))
      , ("largest_difference", show r.largest)
      , ("c_digest", str (pad (showHex r.digest ""))) ]
    pad s = replicate (16 - length s) '0' <> s

total :: [Sample] -> Word64
total = sum . map (.ns)

pct :: Double -> [Sample] -> Word64
pct q samples = sorted !! max 0 (min (length sorted - 1) (ceiling (q * fromIntegral (length sorted)) - 1))
  where sorted = sort (map (.ns) samples)

micros :: Word64 -> String
micros n = fixed 3 (fromIntegral n / 1000)

ratio :: Word64 -> Word64 -> String
ratio a b = fixed 3 (fromIntegral a / fromIntegral b :: Double)

fixed :: Int -> Double -> String
fixed d v = showFFloat (Just d) v ""

topObject :: [(String, String)] -> String
topObject kvs = "{\n  " <> intercalate ",\n  " [ str k <> ": " <> v | (k, v) <- kvs ] <> "\n}\n"

object :: [(String, String)] -> String
object kvs = "{" <> intercalate ", " [ str k <> ": " <> v | (k, v) <- kvs ] <> "}"

list :: [String] -> String
list xs = "[\n    " <> intercalate ",\n    " xs <> "\n  ]"

str :: String -> String
str s = "\"" <> concatMap escape s <> "\""
  where
    escape '"'  = "\\\""
    escape '\\' = "\\\\"
    escape ch | ch < ' ' = let h = showHex (fromEnum ch) "" in "\\u" <> replicate (4 - length h) '0' <> h
              | otherwise = [ch]
