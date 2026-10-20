{-# LANGUAGE MagicHash #-}
{-# LANGUAGE UnboxedTuples #-}
{- | The cover generator in Haskell: 'sleeve_generate' draw for draw, writing
the same description words the C flattens its cover into.
-}
module Sleeve.Generate
    ( Generator(..)
    , newGenerator
    , generate
    ) where

import Sleeve.Colour
import Sleeve.Description

import Control.Monad            ( forM_, when )
import Data.Array.Byte          ( MutableByteArray(..) )
import Data.Bits                ( shiftR, xor )
import Data.Word                ( Word32, Word64 )
import GHC.Exts                 ( RealWorld, newByteArray#, readWord64Array#, writeWord64Array# )
import GHC.IO                   ( IO(..) )
import GHC.Word                 ( Word64(..) )


-- | A description to write into, and the random state.
data Generator = Generator
  { description :: !Description
  , rng         :: !(MutableByteArray RealWorld)
  }

newGenerator :: IO Generator
newGenerator = do
  d <- newDescription
  IO $ \s -> case newByteArray# 8# s of
    (# s1, m #) -> (# s1, Generator { description = d, rng = MutableByteArray m } #)

-- random choices -------------------------------------------------------------------------------

next :: Generator -> IO Word32
next g = IO $ \s -> case g.rng of
  MutableByteArray m -> case readWord64Array# m 0# s of
    (# s1, w #) ->
      let !z0 = W64# w + 0x9e3779b97f4a7c15
          !z1 = (z0 `xor` (z0 `shiftR` 30)) * 0xbf58476d1ce4e5b9
          !z2 = (z1 `xor` (z1 `shiftR` 27)) * 0x94d049bb133111eb
          !z3 = z2 `xor` (z2 `shiftR` 31)
      in case z0 of
           W64# z -> (# writeWord64Array# m 0# z s1, fromIntegral (z3 `shiftR` 32) #)

-- | A number in [0, n).
below :: Generator -> Int -> IO Int
below g n = do
  x <- next g
  pure (fromIntegral ((fromIntegral x * fromIntegral n :: Word64) `shiftR` 32))

-- | A number in [lo, hi].
range :: Generator -> Int -> Int -> IO Int
range g lo hi = (lo +) <$> below g (hi - lo + 1)

chance :: Generator -> Int -> IO Bool
chance g percent = (< percent) <$> below g 100

-- | An index into the weights, chosen in proportion to them.
pick :: Generator -> [Int] -> IO Int
pick g weights = do
  x <- below g (sum weights)
  pure (go 0 x weights)
  where
    go :: Int -> Int -> [Int] -> Int
    go i x (w : rest@(_ : _))
      | x < w     = i
      | otherwise = go (i + 1) (x - w) rest
    go i _ _ = i

-- | One of two values, the first with this chance in a hundred.
either' :: Generator -> Int -> IO a -> IO a -> IO a
either' g percent a b = do
  c <- chance g percent
  if c then a else b

-- palettes -------------------------------------------------------------------------------------

deep, mid, pale, accent, accent2 :: Int
deep    = 0
mid     = 1
pale    = 2
accent  = 3
accent2 = 4

neon :: Int
neon = 5

-- | Each mood's hue range, and each role's hue offset, lightness and chroma ranges.
moods :: [(Int, Int, [[Int]])]
moods =
  [ (275, 320, [[0, 15, 220, 300, 60, 110], [45, 75, 480, 580, 110, 150], [95, 125, 860, 920, 45, 75], [90, 120, 730, 810, 120, 160], [60, 80, 620, 690, 130, 160]])
  , (195, 235, [[0, 10, 260, 360, 50, 90], [-15, 5, 520, 630, 70, 110], [-20, 0, 890, 950, 20, 45], [-150, -120, 850, 910, 70, 110], [-40, -25, 690, 770, 70, 100]])
  , (40, 80, [[-20, 15, 260, 340, 30, 60], [-10, 10, 560, 660, 90, 125], [5, 25, 880, 930, 30, 60], [90, 130, 500, 600, 50, 85], [-35, -20, 500, 580, 110, 140]])
  , (75, 95, [[150, 190, 250, 320, 25, 60], [-55, -40, 560, 640, 140, 170], [0, 10, 930, 965, 20, 35], [-60, -45, 600, 680, 150, 180], [-20, 0, 780, 840, 80, 110]])
  , (80, 100, [[170, 180, 260, 320, 40, 80], [165, 180, 480, 560, 110, 150], [0, 10, 930, 965, 15, 30], [140, 160, 600, 680, 90, 120], [-15, 5, 780, 850, 70, 100]])
  , (260, 295, [[0, 10, 150, 210, 40, 80], [0, 20, 300, 380, 80, 120], [150, 175, 880, 920, 140, 170], [65, 95, 640, 710, 200, 250], [-80, -55, 790, 850, 120, 150]])
  , (200, 250, [[0, 10, 360, 440, 15, 35], [0, 10, 640, 720, 15, 30], [0, 10, 910, 950, 5, 15], [150, 200, 760, 820, 60, 90], [-10, 10, 520, 580, 40, 60]])
  , (125, 165, [[0, 10, 340, 420, 50, 80], [-20, 0, 620, 710, 70, 100], [-45, -25, 910, 945, 45, 70], [165, 200, 720, 790, 100, 130], [-65, -50, 860, 900, 110, 140]])
  , (0, 359, [[0, 0, 300, 400, 80, 120], [0, 20, 550, 640, 100, 140], [150, 210, 860, 920, 30, 60], [160, 200, 700, 780, 100, 140], [20, 40, 750, 820, 60, 90]])
  , (20, 45, [[-10, 10, 170, 220, 15, 30], [0, 10, 540, 600, 160, 190], [40, 60, 900, 940, 40, 60], [30, 50, 780, 840, 140, 170], [-20, -5, 610, 660, 150, 180]])
  ]

moodWeights :: [Int]
moodWeights = [12, 12, 11, 9, 8, 9, 10, 10, 10, 9]

compositionWeights :: Bool -> [Int]
compositionWeights False = [14, 6, 12, 4, 12, 10, 12, 6, 7, 6, 8, 3]
compositionWeights True  = [14, 6, 12, 4, 0, 0, 0, 18, 14, 0, 0, 0]

palette :: Generator -> Int -> IO ()
palette g mood = do
  let (hueLo, hueHi, roles) = moods !! mood
  hue <- range g hueLo hueHi
  forM_ (zip [0 ..] roles) $ \(i, role) -> case role of
    [hLo, hHi, lLo, lHi, cLo, cHi] -> do
      h <- (hue +) <$> range g hLo hHi
      l <- range g lLo lHi
      c <- range g cLo cHi
      writeColour g.description (6 + i) (oklch l c h)
    _other -> pure ()

-- | A palette role's colour.
p :: Generator -> Int -> IO Word32
p g role = readColour g.description (6 + role)

-- building a description -----------------------------------------------------------------------

linearKind, radialKind, checkerKind :: Int
linearKind  = 1
radialKind  = 2
checkerKind = 3

setBase :: Generator -> Word32 -> IO ()
setBase g = writeColour g.description 5

layerCount :: Generator -> IO Int
layerCount g = readWord g.description 3

-- | A new layer of this kind, every field cleared and its extent the whole side; gives its first word.
layer :: Generator -> Int -> Int -> IO Int
layer g kind repeating = do
  n <- layerCount g
  writeWord g.description 3 (n + 1)
  let l = layerAt n
  forM_ [0 .. layerWords - 1] $ \i -> writeWord g.description (l + i) 0
  set g (kindOf l) kind
  set g (repeatingOf l) repeating
  set g (exOf l) unit
  set g (eyOf l) unit
  pure l

set :: Generator -> Int -> Int -> IO ()
set g = writeWord g.description

get :: Generator -> Int -> IO Int
get g = readWord g.description

stop :: Generator -> Int -> Int -> Word32 -> IO ()
stop g l at c = do
  n <- get g (stopCountOf l)
  when (n < 8) $ do
    set g (stopAt l n) at
    writeColour g.description (stopColour l n) c
    set g (stopCountOf l) (n + 1)

-- | A band from @from@ to @to@: two stops, so its edges are hard.
band :: Generator -> Int -> Int -> Int -> Word32 -> IO ()
band g l from to c = stop g l from c >> stop g l to c

linear :: Generator -> Int -> IO Int
linear g angle = do
  l <- layer g linearKind 0
  set g (angleOf l) angle
  pure l

-- | A position along a linear layer's line, as thousandths of its length.
along :: Generator -> Int -> Int -> IO Int
along g l thousandths = do
  angle <- get g (angleOf l)
  pure (fromIntegral (divRound (fromIntegral (lineLength angle) * fromIntegral thousandths) 1000))

radial :: Generator -> Int -> Int -> IO Int
radial g cx cy = do
  l <- layer g radialKind 0
  set g (cxOf l) cx
  set g (cyOf l) cy
  pure l

-- | A hard-edged disc of radius r.
disc :: Generator -> Int -> Int -> Int -> Word32 -> IO ()
disc g cx cy r c = do
  l <- radial g cx cy
  band g l 0 r c
  stop g l r (alpha c 0)

-- | A soft glow of radius r, its colour at @a@ in the centre and fading out.
glow :: Generator -> Int -> Int -> Int -> Int -> Int -> Int -> Word32 -> Int -> IO ()
glow g cx cy ex ey r inner c a = do
  l <- radial g cx cy
  set g (exOf l) ex
  set g (eyOf l) ey
  band g l 0 inner (alpha c a)
  stop g l r (alpha c 0)

lastColour, firstColour :: Generator -> Int -> IO Word32
lastColour g l = do
  n <- get g (stopCountOf l)
  readColour g.description (stopColour l (n - 1))
firstColour g l = readColour g.description (stopColour l 0)

-- compositions ---------------------------------------------------------------------------------

horizon :: Generator -> IO ()
horizon g = do
  night    <- chance g 40
  tilt     <- either' g 60 (pure 0) (range g (-8) 8)
  horizon' <- range g 420 650
  sky      <- linear g (180 + tilt)
  h        <- along g sky horizon'
  end      <- along g sky 1000
  top      <- p g (if night then deep else mid)
  low      <- p g (if night then mid else pale)
  ground   <- p g deep
  edge     <- p g accent2
  stop g sky 0 top
  stop g sky h low
  sun <- chance g 85
  when sun $ do
    setting <- chance g 30
    sunR    <- range g 700 1500
    sunX    <- range g 2000 8000
    sunY    <- if setting then pure (horizon' * 10) else range g 1500 (horizon' * 10 - sunR - 600)
    c       <- p g (if night then pale else accent)
    disc g sunX (max sunY (sunR + 500)) sunR c
  land  <- linear g (180 + tilt)
  band' <- either' g 55 (range g 120 450) (pure 0)
  stop g land h (alpha (if band' /= 0 then edge else ground) 0)
  when (band' /= 0) $ band g land h (h + band') edge
  band g land (h + band') end ground
  lines' <- chance g 35
  when lines' $ do
    ls   <- linear g (180 + tilt)
    gap0 <- range g 400 900
    w    <- range g 60 160
    a    <- range g 70 140
    c    <- (`alpha` a) <$> p g pale
    let go n at gap
          | n + 4 <= 8 && at + w < end = do
              stop g ls at (alpha c 0)
              band g ls at (at + w) c
              stop g ls (at + w) (alpha c 0)
              go (n + 4) (at + w + gap) (gap * 3 `quot` 4 + 120)
          | otherwise = pure n
    n <- go (0 :: Int) (h + band' + gap0) gap0
    when (n == 0) $ layerCount g >>= set g 3 . subtract 1

glowCover :: Generator -> IO ()
glowCover g = do
  wash   <- linear g 180
  dusk   <- chance g 60
  gx     <- range g 3500 6500
  radius <- range g 4500 6500
  stop g wash 0 =<< p g (if dusk then deep else mid)
  when dusk $ do
    at <- range g 4500 6500
    stop g wash at =<< p g mid
  stop g wash unit =<< p g (if dusk then accent2 else pale)
  gy <- (unit +) <$> range g 0 800
  ey <- range g 5500 8000
  a  <- range g 170 220
  c  <- p g pale
  glow g gx gy unit ey radius (radius `quot` 3) c a
  moon <- chance g 55
  when moon $ do
    x    <- range g 2500 7500
    y    <- range g 1800 4000
    size <- range g 700 1300
    disc g x y size =<< p g accent

discCover :: Generator -> IO ()
discCover g = do
  ground <- pick g [4, 2, 4, 0, 1]
  cx     <- either' g 60 (pure 5000) (range g 3500 6500)
  cy     <- either' g 60 (pure 5000) (range g 3500 6500)
  bg     <- p g ground
  setBase g bg
  record <- chance g 35
  if record
    then do
      outer   <- range g 3300 4300
      label   <- range g 900 1400
      pitch   <- range g 90 200
      line    <- alpha <$> p g pale <*> range g 25 45
      vinyl   <- radial g cx cy
      setBase g =<< p g (if ground == deep then mid else deep)
      set g (repeatingOf vinyl) 1
      band g vinyl 0 (pitch `quot` 2) (alpha line 0)
      band g vinyl (pitch `quot` 2) pitch line
      around <- radial g cx cy
      stop g around outer (alpha bg 0)
      stop g around outer bg
      which <- chance g 50
      disc g cx cy label =<< p g (if which then accent else accent2)
      hole <- range g 120 220
      disc g cx cy hole bg
    else do
      rings <- radial g cx cy
      count <- range g 2 3
      first <- below g 4
      let order = [accent, pale, mid, deep]
          ring i at
            | i >= count = pure at
            | otherwise = do
                w <- if i == 0 then range g 1000 2400 else range g 150 900
                let role0 = order !! ((first + i) `mod` 4)
                    role  = if role0 == ground then accent2 else role0
                band g rings at (at + w) =<< p g role
                ring (i + 1) (at + w)
      at <- ring (0 :: Int) 0
      stop g rings at . (`alpha` 0) =<< lastColour g rings
      halo <- chance g 40
      when halo $ do
        hl  <- radial g cx cy
        gap <- range g 300 800
        w   <- range g 120 300
        c   <- alpha <$> firstColour g rings <*> range g 80 140
        stop g hl (at + gap) (alpha c 0)
        band g hl (at + gap) (at + gap + w) c
        stop g hl (at + gap + w) (alpha c 0)
  washed <- chance g 40
  when washed $ do
    wash <- linear g =<< range g 135 200
    c    <- p g pale
    stop g wash 0 (alpha c 0)
    end <- along g wash 1000
    stop g wash end (alpha c 60)

pair :: Generator -> IO ()
pair g = do
  spread <- range g 1800 2300
  y      <- range g 4200 5800
  hub    <- range g 800 1300
  rim    <- range g 250 450
  gap    <- range g 200 400
  dark   <- chance g 60
  ground <- p g (if dark then deep else pale)
  wheel  <- p g (if dark then pale else deep)
  setBase g ground
  strip <- chance g 50
  when strip $ do
    s    <- linear g 180
    half <- range g 1600 2400
    c    <- p g mid
    stop g s (y - half) (alpha c 0)
    band g s (y - half) (y + half) c
    stop g s (y + half) (alpha c 0)
  forM_ [-1, 1] $ \side -> do
    reel <- radial g (5000 + side * spread) y
    band g reel 0 hub wheel
    band g reel hub (hub + gap) ground
    band g reel (hub + gap) (hub + gap + rim) wheel
    stop g reel (hub + gap + rim) (alpha wheel 0)
  spots <- chance g 60
  when spots $ do
    spot <- range g 180 320
    c    <- p g accent
    disc g (5000 - spread) y spot c
    disc g (5000 + spread) y spot c

stripes :: Generator -> IO ()
stripes g = do
  i      <- below g 10
  wobble <- range g (-4) 4
  let angle = [0, 20, 45, 65, 90, 105, 115, 135, 160, 120] !! i + wobble
  solid <- chance g 65
  if solid
    then do
      s     <- linear g angle
      count <- range g 2 4
      first <- below g 3
      set g (repeatingOf s) 1
      let stripe k at = when (k < count) $ do
            let role = (first + k * 2) `mod` 5
            w <- if k == 0 then range g 400 1200
                 else if role >= accent then range g 100 300 else range g 120 700
            band g s at (at + w) =<< p g role
            stripe (k + 1) (at + w)
      stripe (0 :: Int) 0
      setBase g =<< firstColour g s
    else do
      wash  <- linear g =<< range g 150 210
      dark  <- chance g 50
      pitch <- range g 350 800
      w     <- range g 40 120
      ink0  <- p g (if dark then pale else deep)
      stop g wash 0 =<< p g (if dark then deep else mid)
      end <- along g wash 1000
      stop g wash end =<< p g (if dark then mid else pale)
      ls <- linear g angle
      set g (repeatingOf ls) 1
      ink <- alpha ink0 <$> range g 60 120
      band g ls 0 (pitch - w) (alpha ink 0)
      band g ls (pitch - w) pitch ink
      spot <- chance g 40
      when spot $ do
        x    <- range g 2500 7500
        y    <- range g 2500 7500
        size <- range g 1000 1800
        disc g x y size =<< p g accent

bands :: Generator -> IO ()
bands g = do
  ai    <- below g 6
  count <- range g 3 4
  thin  <- either' g 40 (below g count) (pure (-1))
  first <- below g 5
  sheen <- chance g 25
  let angle0 = [180, 90, 180, 0, 270, 180] !! ai
      order  = [pale, mid, accent, deep, accent2]
  if sheen
    then do
      wash <- linear g =<< range g 120 160
      a1   <- range g 3500 4500
      w1   <- range g 200 400
      a2   <- (\d -> a1 + w1 + d) <$> range g 800 1500
      w2   <- range g 150 300
      c1   <- alpha <$> p g pale <*> range g 110 150
      c2   <- alpha <$> p g pale <*> range g 80 110
      stop g wash 0 =<< p g pale
      half <- along g wash 500
      stop g wash half =<< p g mid
      end <- along g wash 1000
      stop g wash end =<< p g deep
      s <- linear g =<< either' g 50 (pure 0) (range g (-10) 10)
      stop g s a1 (alpha c1 0)
      band g s a1 (a1 + w1) c1
      stop g s (a1 + w1) (alpha c1 0)
      stop g s a2 (alpha c2 0)
      band g s a2 (a2 + w2) c2
      stop g s (a2 + w2) (alpha c2 0)
    else do
      angle   <- either' g 30 ((angle0 +) <$> range g (-12) 12) (pure angle0)
      b       <- linear g angle
      length' <- along g b 1000
      weights <- mapM (\i -> if i == thin then range g 3 6 else range g 14 36) [0 .. count - 1]
      let total = sum weights
          go _ _ [] = pure ()
          go i at (w : rest) = do
            let next' = if i == count - 1 then length' else at + length' * w `quot` total
            band g b at next' =<< p g (order !! ((first + i) `mod` 5))
            go (i + 1) next' rest
      go 0 0 weights
      setBase g =<< firstColour g b
      spot <- chance g 30
      when spot $ do
        dr   <- range g 900 1600
        x    <- range g 2500 7500
        y    <- range g 2500 7500
        role <- either' g 50 (pure pale) (pure accent)
        disc g x y dr =<< p g role

split :: Generator -> IO ()
split g = do
  angle  <- range g 20 160
  flip'  <- chance g 50
  s      <- linear g (if flip' then angle + 180 else angle)
  at     <- along g s =<< range g 380 620
  first  <- either' g 50 (pure deep) (pure mid)
  second <- if first == deep then either' g 50 (pure pale) (pure accent) else pure pale
  band g s 0 at =<< p g first
  end <- along g s 1000
  band g s at end =<< p g second
  crossed <- chance g 45
  when crossed $ do
    sAngle <- get g (angleOf s)
    cross  <- linear g . (sAngle +) =<< range g 60 120
    c      <- along g cross =<< range g 350 650
    w      <- range g 500 1500
    colour <- p g accent2
    stop g cross c (alpha colour 0)
    band g cross c (c + w) colour
    stop g cross (c + w) (alpha colour 0)
  spot <- chance g 55
  when spot $ do
    dr <- range g 1000 2200
    x  <- range g 2500 7500
    y  <- range g 2500 7500
    disc g x y dr =<< p g (if second == accent then pale else accent)

bars :: Generator -> IO ()
bars g = do
  ground <- linear g 180
  angle0 <- either' g 70 (pure 90) (pure 180)
  count  <- range g 2 3
  at0    <- range g 1500 3500
  let length' = lineLength angle0
  stop g ground 0 =<< p g deep
  low <- range g 6000 8000
  stop g ground low =<< p g deep
  stop g ground unit =<< p g mid
  angle <- either' g 25 ((angle0 +) <$> range g (-15) 15) (pure angle0)
  let go i at layer'
        | i < count && at < length' - 400 = do
            w  <- range g 150 350
            c  <- p g (if i == 1 then accent2 else if i == 2 then pale else accent)
            bl <- if even i then linear g angle else pure layer'
            stop g bl at (alpha c 0)
            band g bl at (at + w) c
            stop g bl (at + w) (alpha c 0)
            gap <- range g 600 2400
            go (i + 1) (at + w + gap) bl
        | otherwise = pure ()
  go (0 :: Int) at0 0

constellation :: Generator -> IO ()
constellation g = do
  sky   <- linear g 180
  count <- range g 4 9
  d     <- p g deep
  m     <- p g mid
  stop g sky 0 d
  stop g sky unit (mixColour d m 400)
  moon <- chance g 35
  placed0 <- if moon
    then do
      mr <- range g 900 1400
      x  <- range g 2500 7500
      y  <- range g 2000 5000
      disc g x y mr =<< p g pale
      pure [(x, y)]
    else pure []
  let go :: [(Int, Int)] -> Int -> IO ()
      go placed tries = do
        n <- layerCount g
        when (length placed < count + 1 && tries < 40 && n < 12) $ do
          x <- range g 1200 8800
          y <- range g 1200 8800
          let ok = and [ dx * dx + dy * dy >= 1800 * 1800 | (px, py) <- placed, let dx = x - px, let dy = y - py ]
          if not ok
            then go placed (tries + 1)
            else do
              dr   <- range g 200 450
              role <- either' g 50 (pure accent) (either' g 50 (pure accent2) (pure pale))
              disc g x y dr =<< p g role
              go (placed ++ [(x, y)]) (tries + 1)
  go placed0 0

grooves :: Generator -> IO ()
grooves g = do
  cx    <- either' g 50 (range g (-2000) 12000) (either' g 50 (pure 0) (pure unit))
  cy    <- range g (-2000) 12000
  rings <- radial g cx cy
  lines' <- chance g 25
  set g (repeatingOf rings) 1
  if lines'
    then do
      dark  <- chance g 50
      pitch <- range g 350 700
      w     <- range g 40 90
      ink0  <- p g (if dark then pale else deep)
      setBase g =<< p g (if dark then deep else pale)
      ink <- alpha ink0 <$> range g 70 130
      band g rings 0 (pitch - w) (alpha ink 0)
      band g rings (pitch - w) pitch ink
    else do
      two <- chance g 50
      if two
        then do
          ground <- either' g 50 (pure deep) (pure pale)
          w      <- range g 350 900
          w2     <- range g 150 450
          other  <- ([mid, accent, accent2] !!) <$> below g 3
          band g rings 0 w =<< p g ground
          band g rings w (w + w2) =<< p g other
          setBase g =<< p g ground
        else do
          order <- ([[0, 1, 2], [0, 2, 1], [1, 0, 2], [1, 2, 0], [2, 0, 1], [2, 1, 0]] !!) <$> below g 6
          let go _ [] = pure ()
              go at (role : rest) = do
                w <- range g 250 800
                band g rings at (at + w) =<< p g role
                go (at + w) rest
          go 0 order
          setBase g =<< firstColour g rings
  spot <- chance g 20
  when spot $ do
    dr <- range g 900 1600
    x  <- range g 2500 7500
    y  <- range g 2500 7500
    disc g x y dr =<< p g accent

orbs :: Generator -> IO ()
orbs g = do
  ground <- pick g [4, 3, 1, 0, 0]
  first  <- below g 4
  setBase g =<< p g ground
  washed <- chance g 50
  when washed $ do
    wash <- linear g =<< range g 100 170
    stop g wash 0 =<< p g ground
    end <- along g wash 1000
    stop g wash end =<< p g (if ground == deep then mid else deep)
  forM_ [0, 1 :: Int] $ \i -> do
    edge <- chance g 50
    x    <- if edge then (if i == 0 then range g (-1000) 2500 else range g 7500 11000) else range g 2500 7500
    y    <- if edge then (if i == 0 then range g 6000 11000 else range g (-1000) 4000) else range g 2500 7500
    rad  <- if edge then range g 3000 4800 else range g 1800 3200
    let role0 = [pale, accent, accent2, mid] !! ((first + i) `mod` 4)
        role  = if role0 == ground then pale else role0
    orb <- radial g x y
    set g (eyOf orb) =<< either' g 40 (range g 6000 9000) (pure unit)
    c <- p g role
    band g orb 0 rad c
    stop g orb rad (alpha c 0)

checks :: Generator -> IO ()
checks g = do
  c    <- layer g checkerKind 0
  tile <- range g 400 1000
  dark <- chance g 50
  set g (exOf c) tile
  set g (eyOf c) tile
  stop g c 0 =<< p g pale
  stop g c 0 =<< p g (if dark then deep else mid)
  setBase g =<< p g pale
  spot <- chance g 60
  if spot
    then do
      dr <- range g 1800 3200
      d  <- radial g 5000 5000
      band g d 0 dr =<< p g accent
      w  <- range g 300 700
      band g d dr (dr + w) =<< p g pale
      stop g d (dr + w) . (`alpha` 0) =<< p g pale
    else do
      b  <- linear g 180
      at <- range g 5500 7500
      stop g b at . (`alpha` 0) =<< p g accent
      band g b at unit =<< p g accent

-- | Writes the description of the cover for this seed.
generate :: Generator -> Word64 -> IO ()
generate g seed = do
  writeSeed (mix (seed `xor` 0x736c65657665))
  set g 0 1
  set g 3 0
  mood        <- pick g moodWeights
  composition <- pick g (compositionWeights (mood == neon))
  set g 1 composition
  set g 2 mood
  palette g mood
  set g 4 5
  setBase g =<< p g deep
  case composition of
    0  -> horizon g
    1  -> glowCover g
    2  -> discCover g
    3  -> pair g
    4  -> stripes g
    5  -> bands g
    6  -> split g
    7  -> bars g
    8  -> constellation g
    9  -> grooves g
    10 -> orbs g
    _  -> checks g
  n <- layerCount g
  forM_ [layerAt n .. words' - 1] $ \i -> set g i 0
  where
    writeSeed (W64# w) = IO $ \s -> case g.rng of
      MutableByteArray m -> (# writeWord64Array# m 0# w s, () #)
