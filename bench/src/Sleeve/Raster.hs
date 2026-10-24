{-# OPTIONS_GHC -fno-full-laziness -fmax-worker-args=40 #-}
{-# LANGUAGE MagicHash #-}
{-# LANGUAGE UnboxedTuples #-}
{- | The rasterizer in Haskell: 'sleeve_render' written as strict loops over a
description's prepared layers, writing into a preallocated buffer, with the
same single-precision operations in the same order.

Every pixel loop is a join point over strict, unboxed state, and every colour
is a strict four-float product its producers return unboxed, so the pixels
allocate nothing on the heap; checking and preparing the description's layers
allocates a few kilobytes a render.
-}
module Sleeve.Raster
    ( Scratch
    , newScratch
    , render
    ) where

import Sleeve.Colour            ( cosD, sinD )
import Sleeve.Description

import Control.Monad            ( forM_ )
import Data.Array.Byte          ( MutableByteArray(..) )
import Data.Bits                ( shiftR, (.&.) )
import GHC.Exts                 ( ByteArray#, Float(..), Int(..), MutableByteArray#, RealWorld, State#, float2Int#
                                , indexFloatArray#, indexInt32Array#, int2Float#, int32ToInt#, intToInt32#
                                , newByteArray#, readInt32Array#, unsafeFreezeByteArray#, wordToWord8#
                                , writeFloatArray#, writeInt32Array#, writeWord8Array#, int2Word# )
import GHC.IO                   ( IO(..) )


-- | Room for a render's prepared layers and each row's column bounds.
data Scratch = Scratch
  { ready  :: !(MutableByteArray RealWorld)
  , bounds :: !(MutableByteArray RealWorld)
  }

newScratch :: IO Scratch
newScratch = IO $ \s -> case newByteArray# layers# s of
  (# s1, r #) -> case newByteArray# 96# s1 of
    (# s2, b #) -> (# s2, Scratch { ready = MutableByteArray r, bounds = MutableByteArray b } #)
  where !(I# layers#) = 4 * 12 * stride

-- | A colour, premultiplied, in [0, 255].
data V4 = V4 {-# UNPACK #-} !Float {-# UNPACK #-} !Float {-# UNPACK #-} !Float {-# UNPACK #-} !Float

zero :: V4
zero = V4 0 0 0 0

-- | The trapezoid a pixel's square projects to along a gradient.
data Kernel = Kernel
  { t     :: {-# UNPACK #-} !Float
  , h1    :: {-# UNPACK #-} !Float
  , h2    :: {-# UNPACK #-} !Float
  , invA  :: {-# UNPACK #-} !Float
  , invAb :: {-# UNPACK #-} !Float
  }

-- prepared layers ------------------------------------------------------------------------------

-- | Each prepared layer's words: integers for its kind, repetition and stop count, then floats.
stride, kindF, repF, countF, atF, colourF, periodF, meanF, dxF, dyF, startF, cxF, cyF, kF, reachF, tileF :: Int
stride  = 64
kindF   = 0
repF    = 1
countF  = 2
atF     = 3
colourF = 11
periodF = 43
meanF   = 44
dxF     = 48
dyF     = 49
startF  = 50
cxF     = 51
cyF     = 52
kF      = 53
reachF  = 54
tileF   = 55

fl :: ByteArray# -> Int -> Float
fl a (I# i) = F# (indexFloatArray# a i)
{-# INLINE fl #-}

int :: ByteArray# -> Int -> Int
int a (I# i) = I# (int32ToInt# (indexInt32Array# a i))
{-# INLINE int #-}

putF :: MutableByteArray RealWorld -> Int -> Float -> IO ()
putF (MutableByteArray m) (I# i) (F# v) = IO $ \s -> (# writeFloatArray# m i v s, () #)

putI :: MutableByteArray RealWorld -> Int -> Int -> IO ()
putI (MutableByteArray m) (I# i) (I# v) = IO $ \s -> (# writeInt32Array# m i (intToInt32# v) s, () #)

-- | Whether the description is one the rasterizer accepts, as 'sleeve_check' decides.
valid :: Description -> IO Bool
valid d = do
  count <- readWord d 3
  base  <- readColour d 5
  if count < 0 || count > 12 || base `shiftR` 24 /= 255 then pure False else and <$> mapM layer' [0 .. count - 1]
  where
    far = 100 * 10000
    out v = v < -far || v > far
    layer' i = do
      let l = layerAt i
      kind <- readWord d (kindOf l)
      rep  <- readWord d (repeatingOf l)
      n    <- readWord d (stopCountOf l)
      cx   <- readWord d (cxOf l)
      cy   <- readWord d (cyOf l)
      ex   <- readWord d (exOf l)
      ey   <- readWord d (eyOf l)
      ats  <- mapM (readWord d . stopAt l) [0 .. min 8 (max 0 n) - 1]
      let ordered = and (zipWith (<=) ats (drop 1 ats))
          lastAt  = if null ats then 0 else ats !! (length ats - 1)
          headAt  = if null ats then 0 else ats !! 0
          shape = case kind of
            1 -> True
            2 -> ex > 0 && ey > 0 && ex <= far && ey <= far
            3 -> n == 2 && rep == 0 && ex > 0 && ex <= far
            _ -> False
      pure $ n >= 1 && n <= 8 && rep >= 0 && rep <= 1 && not (out cx || out cy) && not (any out ats) && ordered
          && (rep == 0 || (headAt == 0 && lastAt > 0)) && shape

-- | Prepares each layer: positions in sides, colours premultiplied in [0, 255].
prepare :: Scratch -> Description -> Int -> IO ()
prepare scratch d count = forM_ [0 .. count - 1] $ \i -> do
  let l = layerAt i
      o = stride * i
      r = scratch.ready
      unitF = 1 / 10000 :: Float
  kind  <- readWord d (kindOf l)
  rep   <- readWord d (repeatingOf l)
  n     <- readWord d (stopCountOf l)
  angle <- readWord d (angleOf l)
  cx    <- readWord d (cxOf l)
  cy    <- readWord d (cyOf l)
  ex    <- readWord d (exOf l)
  ey    <- readWord d (eyOf l)
  putI r (o + kindF) kind
  putI r (o + repF) rep
  putI r (o + countF) n
  stops <- mapM (\j -> (,) <$> readWord d (stopAt l j) <*> readColour d (stopColour l j)) [0 .. n - 1]
  let ats     = [ fromIntegral at * unitF | (at, _) <- stops ]
      channel c k = fromIntegral ((c `shiftR` k) .&. 255) :: Float
      colours = [ [ channel c 0 * a, channel c 8 * a, channel c 16 * a, channel c 24 ]
                | (_, c) <- stops, let a = channel c 24 * (1 / 255) ]
      period  = ats !! (n - 1)
      means   = if rep == 0 then [0, 0, 0, 0]
                else map (/ period) $ foldl
                       (\acc (c0, c1, a0, a1) -> zipWith3 (\m x y -> m + 0.5 * (x + y) * (a1 - a0)) acc c0 c1)
                       [0, 0, 0, 0] (zip4' colours (drop 1 colours) ats (drop 1 ats))
      dx      = fromIntegral (sinD angle) * (1 / 65536) :: Float
      dy      = negate (fromIntegral (cosD angle)) * (1 / 65536) :: Float
      len     = abs dx + abs dy
      alphas  = [ c `shiftR` 24 | (_, c) <- stops ]
      reach | kind == 2 && rep == 0 && alphas !! (n - 1) == 0 =
                foldl (\acc (j, a) -> if a /= 0 && j + 1 < n then ats !! (j + 1) else acc) (ats !! 0) (zip [0 ..] alphas)
            | otherwise = -1
  forM_ (zip [0 ..] ats) $ \(j, at) -> putF r (o + atF + j) at
  forM_ (zip [0 ..] colours) $ \(j, c) -> forM_ (zip [0 ..] c) $ \(ch, v) -> putF r (o + colourF + 4 * j + ch) v
  putF r (o + periodF) period
  forM_ (zip [0 ..] means) $ \(ch, v) -> putF r (o + meanF + ch) v
  putF r (o + dxF) dx
  putF r (o + dyF) dy
  putF r (o + startF) (0.5 * dx + 0.5 * dy - 0.5 * len)
  putF r (o + cxF) (fromIntegral cx * unitF)
  putF r (o + cyF) (fromIntegral cy * unitF)
  putF r (o + kF) (if ey > 0 then fromIntegral ex / fromIntegral ey else 1)
  putF r (o + reachF) reach
  putF r (o + tileF) (fromIntegral ex * unitF)
  where
    zip4' (a : as) (b : bs) (c : cs) (e : es) = (a, b, c, e) : zip4' as bs cs es
    zip4' _ _ _ _ = []

-- sampling -------------------------------------------------------------------------------------

-- | The floor of x; every float of magnitude 2^23 or more is already whole.
floorF :: Float -> Float
floorF x@(F# x#)
  | not (x > -8388608 && x < 8388608) = x
  | otherwise = let t' = F# (int2Float# (float2Int# x#)) in if t' > x then t' - 1 else t'
{-# INLINE floorF #-}

sqrtF :: Float -> Float
sqrtF x = if x > 0 then sqrt x else 0
{-# INLINE sqrtF #-}

colourAt :: ByteArray# -> Int -> Int -> V4
colourAt a o i = V4 (fl a c) (fl a (c + 1)) (fl a (c + 2)) (fl a (c + 3))
  where c = o + colourF + 4 * i
{-# INLINE colourAt #-}

-- | The colour at t, already within a repeating gradient's period.
point :: ByteArray# -> Int -> Float -> V4
point a !o !t'
  | t' <= at 0 = colourAt a o 0
  | otherwise  = go 0
  where
    n    = int a (o + countF)
    at i = fl a (o + atF + i)
    go !i
      | i + 1 >= n     = colourAt a o (n - 1)
      | t' < at (i + 1) =
          let !f = (t' - at i) / (at (i + 1) - at i)
              !(V4 a0 a1 a2 a3) = colourAt a o i
              !(V4 b0 b1 b2 b3) = colourAt a o (i + 1)
          in V4 (a0 + (b0 - a0) * f) (a1 + (b1 - a1) * f) (a2 + (b2 - a2) * f) (a3 + (b3 - a3) * f)
      | otherwise = go (i + 1)

weight :: Kernel -> Float -> Float
weight !k !x
  | u <= k.h2 || k.invAb == 0 = k.invA
  | u' > 0    = u' * k.invAb
  | otherwise = 0
  where
    u  = abs (x - k.t)
    u' = k.h1 - u
{-# INLINE weight #-}

{- | Adds the integral of a linear colour piece times the kernel over
[x0, x1]: the piece's colour is c0 at @origin@ and changes by @slope@ a side.
Simpson's rule is exact for the product.
-}
piece :: Kernel -> Float -> Float -> Float -> V4 -> V4 -> V4 -> V4
piece !k !x0' !x1' !origin (V4 c0 c1 c2 c3) (V4 s0 s1 s2 s3) acc@(V4 e0 e1 e2 e3)
  | not (x1 > x0) = V4 e0 e1 e2 e3
  | otherwise =
      let !acc1 = if in1 then simpson x0 m1 acc else acc
          !p1   = if in1 then m1 else x0
          !acc2 = if in2 then simpson p1 m2 acc1 else acc1
          !p2   = if in2 then m2 else p1
      in simpson p2 x1 acc2
  where
    lo  = k.t - k.h1
    hi  = k.t + k.h1
    x0  = if x0' < lo then lo else x0'
    x1  = if x1' > hi then hi else x1'
    m1  = k.t - k.h2
    m2  = k.t + k.h2
    in1 = m1 > x0 && m1 < x1
    in2 = m2 > x0 && m2 < x1
    simpson !l !r (V4 a0 a1 a2 a3) =
      let !m  = 0.5 * (l + r)
          !wl = weight k l
          !wm = weight k m
          !wr = weight k r
          !s  = (r - l) * (1 / 6)
          ch c sl acc' =
            let fl' = c + sl * (l - origin)
                fm  = c + sl * (m - origin)
                fr  = c + sl * (r - origin)
            in acc' + s * (fl' * wl + 4 * fm * wm + fr * wr)
      in V4 (ch c0 s0 a0) (ch c1 s1 a1) (ch c2 s2 a2) (ch c3 s3 a3)
    {-# INLINE simpson #-}

-- | Adds the gradient's pieces shifted by @offset@, over the kernel's support.
pieces :: ByteArray# -> Int -> Kernel -> Float -> V4 -> V4
pieces a !o !k !offset (V4 i0 i1 i2 i3) = go 0 i0 i1 i2 i3
  where
    n    = int a (o + countF)
    rep  = int a (o + repF) /= 0
    at i = fl a (o + atF + i)
    -- the sums are four floats rather than a V4, so they stay in registers
    go !i !e0 !e1 !e2 !e3
      | i + 1 >= n = ends e0 e1 e2 e3
      | len <= 0   = go (i + 1) e0 e1 e2 e3
      | otherwise  =
          let !(V4 a0 a1 a2 a3) = colourAt a o i
              !(V4 b0 b1 b2 b3) = colourAt a o (i + 1)
              !slope = V4 ((b0 - a0) / len) ((b1 - a1) / len) ((b2 - a2) / len) ((b3 - a3) / len)
          in case piece k (at i + offset) (at (i + 1) + offset) (at i + offset) (V4 a0 a1 a2 a3) slope (V4 e0 e1 e2 e3) of
               V4 f0 f1 f2 f3 -> go (i + 1) f0 f1 f2 f3
      where len = at (i + 1) - at i
    ends !e0 !e1 !e2 !e3
      | rep = V4 e0 e1 e2 e3
      | otherwise =
          let !lo   = k.t - k.h1
              !hi   = k.t + k.h1
              !acc1 = if lo < at 0 then piece k lo (at 0) lo (colourAt a o 0) zero (V4 e0 e1 e2 e3) else V4 e0 e1 e2 e3
          in if hi > at (n - 1) then piece k (at (n - 1)) hi hi (colourAt a o (n - 1)) zero acc1 else acc1
{-# INLINE pieces #-}

{- | The gradient's colour over a pixel whose footprint along it is a box a
wide convolved with a box b wide, centred on t.
-}
sample :: ByteArray# -> Int -> Float -> Float -> Float -> V4
sample a !o !t' !a0 !b0
  | inPeriod && fits 0 below0 = point a o local
  | wide <= 0 = point a o local
  | otherwise =
      let !b' = if narrow < wide * 1e-4 then 0 else narrow
          -- a repeating gradient is integrated around local, so the periods are small whole numbers
          !k  = Kernel
                  { t     = if rep then local else t'
                  , h1    = 0.5 * (wide + b')
                  , h2    = 0.5 * (wide - b')
                  , invA  = 1 / wide
                  , invAb = if b' > 0 then 1 / (wide * b') else 0
                  }
          !first = floorF ((local - k.h1) / period)
          !final = floorF ((local + k.h1) / period)
          periods :: Int -> Int -> Float -> Float -> Float -> Float -> V4
          periods !f !left !e0 !e1 !e2 !e3
            | left >= 0 = case pieces a o k (fromIntegral f * period) (V4 e0 e1 e2 e3) of
                V4 f0 f1 f2 f3 -> periods (f + 1) (left - 1) f0 f1 f2 f3
            | otherwise = V4 e0 e1 e2 e3
      in if not rep then pieces a o k 0 zero
         else if not (final - first <= 32 && first >= -1048576 && final <= 1048576)
           then V4 (fl a (o + meanF)) (fl a (o + meanF + 1)) (fl a (o + meanF + 2)) (fl a (o + meanF + 3))
         else periods (truncate first) (truncate (final - first)) 0 0 0 0
  where
    !wide   = if a0 < b0 then b0 else a0
    !narrow = if a0 < b0 then a0 else b0
    !n      = int a (o + countF)
    !rep    = int a (o + repF) /= 0
    !period = fl a (o + periodF)
    at i    = fl a (o + atF + i)
    !h      = 0.5 * (wide + narrow)
    !local  = if rep then t' - period * floorF (t' / period) else t'
    !lo     = local - h
    !hi     = local + h
    inPeriod = not rep || (lo >= 0 && hi <= period)
    below0 = if rep then 0 else -1e30
    above0 = if rep then period else 1e30
    -- whether the support lies within one piece, the colour linear there
    fits !i !bl
      | i >= n         = lo >= bl && hi <= above0
      | at i <= local  = fits (i + 1) (if at i > bl then at i else bl)
      | otherwise      = lo >= bl && hi <= at i
{-# INLINE sample #-}

-- | A radial layer over a footprint fw x fh at offset (u, v) from its centre, distance t.
radialSample :: ByteArray# -> Int -> Float -> Float -> Float -> Float -> Float -> V4
radialSample a !o !u !v !t' !fw !fh
  | t' > 0    = sample a o t' (abs u / t' * fw) (abs v / t' * fh)
  | otherwise = sample a o t' fw fh
{-# INLINE radialSample #-}

-- | The share of [x0, x1] on odd squares of a row of squares @tile@ wide from 0.
oddShare :: Float -> Float -> Float -> Float
oddShare !x0 !x1 !tile = (f1 - f0) / (q1 - q0)
  where
    q0 = x0 / tile
    q1 = x1 / tile
    n0 = floorF q0
    n1 = floorF q1
    e0 = floorF (n0 * 0.5)
    e1 = floorF (n1 * 0.5)
    f0 = e0 + (if n0 - 2 * e0 > 0.5 then q0 - n0 else 0)
    f1 = e1 + (if n1 - 2 * e1 > 0.5 then q1 - n1 else 0)

-- | A layer's colour over the pixel centred at (px, py).
layerColour :: ByteArray# -> Int -> Float -> Float -> Float -> Float -> V4
layerColour a !o !invW !invH !px !py = case int a (o + kindF) of
  1 -> let !dx = fl a (o + dxF)
           !dy = fl a (o + dyF)
       in sample a o (px * dx + py * dy - fl a (o + startF)) (abs dx * invW) (abs dy * invH)
  2 -> let !k'    = fl a (o + kF)
           !u     = px - fl a (o + cxF)
           !v     = (py - fl a (o + cyF)) * k'
           !t'    = sqrtF (u * u + v * v)
           !reach = invW + k' * invH
       in if t' >= 2 * reach
            then radialSample a o u v t' invW (k' * invH)
            else
              -- near the centre the rings curve within the pixel, so it is split in 4 x 4
              let sub :: Int -> Int -> V4 -> V4
                  sub sy sx acc@(V4 r0 r1 r2 r3)
                    | sy >= 4 = acc
                    | sx >= 4 = sub (sy + 1) 0 acc
                    | otherwise =
                        let !su = u + (fromIntegral sx - 1.5) * 0.25 * invW
                            !sv = v + (fromIntegral sy - 1.5) * 0.25 * invH * k'
                            !(V4 p0 p1 p2 p3) = radialSample a o su sv (sqrtF (su * su + sv * sv)) (0.25 * invW) (0.25 * k' * invH)
                        in sub sy (sx + 1) (V4 (r0 + p0 * (1 / 16)) (r1 + p1 * (1 / 16)) (r2 + p2 * (1 / 16)) (r3 + p3 * (1 / 16)))
              in sub 0 0 zero
  _ -> let !cx   = fl a (o + cxF)
           !cy   = fl a (o + cyF)
           !tile = fl a (o + tileF)
           !sx   = oddShare (px - 0.5 * invW - cx) (px + 0.5 * invW - cx) tile
           !sy   = oddShare (py - 0.5 * invH - cy) (py + 0.5 * invH - cy) tile
           !odd' = sx * (1 - sy) + sy * (1 - sx)
           !(V4 a0 a1 a2 a3) = colourAt a o 0
           !(V4 b0 b1 b2 b3) = colourAt a o 1
       in V4 (a0 + (b0 - a0) * odd') (a1 + (b1 - a1) * odd') (a2 + (b2 - a2) * odd') (a3 + (b3 - a3) * odd')

-- rendering ------------------------------------------------------------------------------------

{- | Renders the description into @out@, @width@ by @height@ RGBA pixels with
rows @4 * width@ bytes apart. Gives 0, or -1 for a description the
rasterizer refuses.
-}
render :: Scratch -> Description -> Int -> Int -> MutableByteArray RealWorld -> IO Int
render scratch d width height (MutableByteArray out) = do
  ok <- valid d
  if not ok || width < 1 || height < 1 || width > 16384 || height > 16384 then pure (-1) else do
    count <- readWord d 3
    base  <- readColour d 5
    prepare scratch d count
    let !br = fromIntegral (base .&. 255) :: Float
        !bg = fromIntegral ((base `shiftR` 8) .&. 255) :: Float
        !bb = fromIntegral ((base `shiftR` 16) .&. 255) :: Float
    case (scratch.ready, scratch.bounds) of
      (MutableByteArray r, MutableByteArray bm) -> IO $ \s0 -> case unsafeFreezeByteArray# r s0 of
        (# s1, a #) -> case rows a bm count br bg bb 0 s1 of
          s2 -> (# s2, 0 #)
  where
    !invW = 1 / fromIntegral width :: Float
    !invH = 1 / fromIntegral height :: Float
    !widthF = fromIntegral width :: Float

    rows :: ByteArray# -> MutableByteArray# RealWorld -> Int -> Float -> Float -> Float -> Int -> State# RealWorld -> State# RealWorld
    rows a bm count br bg bb = row
      where
        row !y s
          | y >= height = s
          | otherwise = pixels 0 (columns 0 s)
          where
            !py = (fromIntegral y + 0.5) * invH
            -- the columns where each radial layer with a reach can show
            columns !i s'
              | i >= count = s'
              | int a (o + kindF) == 2 && reach >= 0 =
                  let !v     = abs ((py - fl a (o + cyF)) * k')
                      !r     = reach + (invW + k' * invH)
                      !span' = sqrtF (r * r - v * v)
                      !first = (fl a (o + cxF) - span') * widthF - 1
                      !final = (fl a (o + cxF) + span') * widthF + 1
                      !lo | first <= 0      = 0
                          | first >= widthF = width
                          | otherwise       = truncateF first
                      !hi | final <= 0      = 0
                          | final >= widthF = width
                          | otherwise       = truncateF final + 1
                  in if v >= r then columns (i + 1) (put bm (2 * i) 0 (put bm (2 * i + 1) 0 s'))
                     else columns (i + 1) (put bm (2 * i) lo (put bm (2 * i + 1) hi s'))
              | otherwise = columns (i + 1) (put bm (2 * i) 0 (put bm (2 * i + 1) width s'))
              where
                !o     = stride * i
                !reach = fl a (o + reachF)
                !k'    = fl a (o + kF)
            pixels !x s'
              | x >= width = row (y + 1) s'
              | otherwise = layers 0 br bg bb 255 s'
              where
                !px = (fromIntegral x + 0.5) * invW
                layers !i !c0 !c1 !c2 !c3 s''
                  | i >= count =
                      let !at = 4 * (y * width + x)
                      in pixels (x + 1) (byte at c0 (byte (at + 1) c1 (byte (at + 2) c2 (byte (at + 3) 255 s''))))
                  | otherwise = case get bm (2 * i) s'' of
                      (# s3, lo #) -> case get bm (2 * i + 1) s3 of
                        (# s4, hi #)
                          | x < lo || x >= hi -> layers (i + 1) c0 c1 c2 c3 s4
                          | otherwise ->
                              let !(V4 r0 r1 r2 r3) = layerColour a (stride * i) invW invH px py
                                  !keep = 1 - r3 * (1 / 255)
                              in layers (i + 1) (r0 + c0 * keep) (r1 + c1 * keep) (r2 + c2 * keep) (r3 + c3 * keep) s4

    byte :: Int -> Float -> State# RealWorld -> State# RealWorld
    byte (I# at) v s =
      let !w = v + 0.5
          !(I# b) | w <= 0    = 0
                  | w >= 255  = 255
                  | otherwise = truncateF w
      in writeWord8Array# out at (wordToWord8# (int2Word# b)) s
    {-# INLINE byte #-}

truncateF :: Float -> Int
truncateF (F# f) = I# (float2Int# f)
{-# INLINE truncateF #-}

put :: MutableByteArray# RealWorld -> Int -> Int -> State# RealWorld -> State# RealWorld
put m (I# i) (I# v) = writeInt32Array# m i (intToInt32# v)
{-# INLINE put #-}

get :: MutableByteArray# RealWorld -> Int -> State# RealWorld -> (# State# RealWorld, Int #)
get m (I# i) s = case readInt32Array# m i s of (# s1, v #) -> (# s1, I# (int32ToInt# v) #)
{-# INLINE get #-}
