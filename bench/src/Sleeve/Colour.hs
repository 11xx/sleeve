{- | sleeve's integer arithmetic and colour: the cosine and sRGB tables,
rounding division, the seed mixer and hash, and OKLCH to 8-bit sRGB, each as
'sleeve.h' computes it.
-}
module Sleeve.Colour
    ( unit
    , cosD
    , sinD
    , divRound
    , lineLength
    , mix
    , hash
    , oklch
    , mixColour
    , alpha
    , alphaOf
    , pack
    ) where

import Data.Array.Base          ( unsafeAt )
import Data.Array.Unboxed       ( UArray, listArray )
import Data.Bits                ( shiftL, shiftR, xor, (.&.), (.|.) )
import Data.Int                 ( Int64 )
import Data.Word                ( Word32, Word64, Word8 )


-- | A side of the cover.
unit :: Int
unit = 10000

-- | @cos(d degrees)@ in units of 1/65536, for d in [0, 90].
cosTable :: UArray Int Int
cosTable = listArray (0, 90)
  [ 65536, 65526, 65496, 65446, 65376, 65287, 65177, 65048, 64898, 64729, 64540, 64332
  , 64104, 63856, 63589, 63303, 62997, 62672, 62328, 61966, 61584, 61183, 60764, 60326
  , 59870, 59396, 58903, 58393, 57865, 57319, 56756, 56175, 55578, 54963, 54332, 53684
  , 53020, 52339, 51643, 50931, 50203, 49461, 48703, 47930, 47143, 46341, 45525, 44695
  , 43852, 42995, 42126, 41243, 40348, 39441, 38521, 37590, 36647, 35693, 34729, 33754
  , 32768, 31772, 30767, 29753, 28729, 27697, 26656, 25607, 24550, 23486, 22415, 21336
  , 20252, 19161, 18064, 16962, 15855, 14742, 13626, 12505, 11380, 10252, 9121, 7987
  , 6850, 5712, 4572, 3430, 2287, 1144, 0 ]

-- | @round(65535 * linear(v / 255))@ for each 8-bit sRGB value v.
linearTable :: UArray Int Int64
linearTable = listArray (0, 255)
  [ 0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219
  , 241, 264, 288, 313, 340, 367, 396, 427, 458, 491, 526, 562
  , 599, 637, 677, 718, 761, 805, 851, 898, 947, 997, 1048, 1101
  , 1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863
  , 1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866
  , 2961, 3058, 3157, 3258, 3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129
  , 4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124, 5257, 5392, 5530, 5669
  , 5810, 5953, 6099, 6246, 6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500
  , 7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889, 9072, 9258, 9445, 9635
  , 9828, 10022, 10219, 10417, 10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090
  , 12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909, 14146, 14387, 14629, 14874
  , 15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001
  , 18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481
  , 21787, 22096, 22407, 22721, 23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325
  , 25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094, 28452, 28813, 29176, 29542
  , 29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143
  , 34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138
  , 39572, 40009, 40449, 40891, 41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534
  , 45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359, 48850, 49344, 49841, 50341
  , 50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567
  , 57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221
  , 63795, 64372, 64952, 65535 ]

-- | @cos(d degrees)@ in units of 1/65536, for any whole d.
cosD :: Int -> Int
cosD d0
  | d <= 90   = table d
  | d <= 180  = negate (table (180 - d))
  | d <= 270  = negate (table (d - 180))
  | otherwise = table (360 - d)
  where
    d       = d0 `mod` 360
    table i = cosTable `unsafeAt` i

sinD :: Int -> Int
sinD d = cosD (90 - d)

-- | @n / d@ to the nearest, halves up, for @d > 0@.
divRound :: Int64 -> Int64 -> Int64
divRound n d = (2 * n + d) `div` (2 * d)
{-# INLINE divRound #-}

-- | The length of a linear gradient's line at this angle, in side units.
lineLength :: Int -> Int
lineLength angle = fromIntegral (divRound (fromIntegral (abs (sinD angle) + abs (cosD angle)) * fromIntegral unit) 65536)

-- | MurmurHash3's 64-bit finalizer.
mix :: Word64 -> Word64
mix h0 = h3 `xor` (h3 `shiftR` 33)
  where
    h1 = h0 `xor` (h0 `shiftR` 33)
    h2 = (h1 * 0xff51afd7ed558ccd) `xor` ((h1 * 0xff51afd7ed558ccd) `shiftR` 33)
    h3 = h2 * 0xc4ceb9fe1a85ec53

-- | FNV-1a over the bytes, mixed: the seed for a key.
hash :: [Word8] -> Word64
hash = mix . foldl (\h b -> (h `xor` fromIntegral b) * 0x100000001b3) 0xcbf29ce484222325

-- | The 8-bit sRGB value nearest a linear value in units of 1/65535, the lower on a tie.
encode :: Int64 -> Word32
encode x = go 0 256
  where
    go :: Int -> Int -> Word32
    go lo hi
      | hi - lo <= 1 = fromIntegral lo
      | linearTable `unsafeAt` (mid - 1) + linearTable `unsafeAt` mid < 2 * x = go mid hi
      | otherwise = go lo mid
      where mid = (lo + hi) `div` 2

{- | OKLCH to sRGB: lightness and chroma in thousandths, hue in degrees. Chroma
is lowered in steps of 0.004 until the colour is inside the sRGB gamut.
-}
oklch :: Int -> Int -> Int -> Word32
oklch lightness chroma0 hue = go (fromIntegral chroma0)
  where
    l = divRound (fromIntegral lightness * 65536) 1000
    cosH = fromIntegral (cosD hue)
    sinH = fromIntegral (sinD hue)
    cube v = divRound (v * v * v) (65536 * 65536)
    inside v = v >= -64 && v <= 65600
    go :: Int64 -> Word32
    go chroma
      | chroma <= 0 || (inside red && inside green && inside blue) = pack (encode red) (encode green) (encode blue) 255
      | otherwise = go (chroma - 4)
      where
        a     = divRound (chroma * cosH) 1000
        b     = divRound (chroma * sinH) 1000
        lc    = cube (l + divRound (25974 * a + 14143 * b) 65536)
        mc    = cube (l + divRound (-6918 * a - 4185 * b) 65536)
        sc    = cube (l + divRound (-5864 * a - 84639 * b) 65536)
        red   = divRound (267173 * lc - 216774 * mc + 15137 * sc) 65536
        green = divRound (-83128 * lc + 171033 * mc - 22369 * sc) 65536
        blue  = divRound (-275 * lc - 46099 * mc + 111910 * sc) 65536

pack :: Word32 -> Word32 -> Word32 -> Word32 -> Word32
pack r g b a = r .|. (g `shiftL` 8) .|. (b `shiftL` 16) .|. (a `shiftL` 24)

-- | a and b mixed, @thousandths@ of the way to b, in sRGB, opaque.
mixColour :: Word32 -> Word32 -> Int -> Word32
mixColour a b t = pack (channel 0) (channel 8) (channel 16) 255
  where
    channel :: Int -> Word32
    channel at = let ca = fromIntegral ((a `shiftR` at) .&. 255) :: Int64
                     cb = fromIntegral ((b `shiftR` at) .&. 255)
                 in fromIntegral (ca + divRound ((cb - ca) * fromIntegral t) 1000)

alpha :: Word32 -> Int -> Word32
alpha c a = (c .&. 0xffffff) .|. (fromIntegral a `shiftL` 24)

alphaOf :: Word32 -> Int
alphaOf c = fromIntegral (c `shiftR` 24)
