{-# LANGUAGE MagicHash #-}
{-# LANGUAGE UnliftedFFITypes #-}
{- | The C library ('cbits/bench.c' over 'sleeve.h'), one unsafe call per
cover over the same unpinned arrays the Haskell port writes.
-}
module Sleeve.Ffi
    ( describeC
    , coverC
    , renderC
    , hashBytes
    ) where

import Sleeve.Description       ( Description(..) )

import Data.Array.Byte          ( MutableByteArray(..) )
import Data.Word                ( Word64 )
import GHC.Exts                 ( MutableByteArray#, RealWorld )


foreign import ccall unsafe "sleeve_bench_describe"
  c_describe :: Word64 -> MutableByteArray# RealWorld -> IO Int

foreign import ccall unsafe "sleeve_bench_cover"
  c_cover :: Word64 -> Int -> MutableByteArray# RealWorld -> IO Int

foreign import ccall unsafe "sleeve_bench_render"
  c_render :: MutableByteArray# RealWorld -> Int -> Int -> MutableByteArray# RealWorld -> IO Int

foreign import ccall unsafe "sleeve_bench_hash"
  c_hash :: MutableByteArray# RealWorld -> Int -> IO Word64

-- | The seed's description, as the C generates and flattens it.
describeC :: Word64 -> Description -> IO ()
describeC seed (Description (MutableByteArray m)) = () <$ c_describe seed m

-- | Generates the seed's cover and renders it @size@ pixels square, in C.
coverC :: Word64 -> Int -> MutableByteArray RealWorld -> IO Int
coverC seed size (MutableByteArray m) = c_cover seed size m
{-# INLINE coverC #-}

-- | Renders a description @width@ by @height@ pixels, in C.
renderC :: Description -> Int -> Int -> MutableByteArray RealWorld -> IO Int
renderC (Description (MutableByteArray d)) width height (MutableByteArray m) = c_render d width height m

-- | 'sleeve_hash' over the first @n@ bytes.
hashBytes :: MutableByteArray RealWorld -> Int -> IO Word64
hashBytes (MutableByteArray m) = c_hash m
