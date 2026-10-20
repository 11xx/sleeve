{-# LANGUAGE MagicHash #-}
{-# LANGUAGE UnboxedTuples #-}
{- | A cover's description as 32-bit words, laid out as 'cbits/bench.h' says:
the header, then twelve layers of 'layerWords' words each. Colours are
@r | g << 8 | b << 16 | a << 24@.
-}
module Sleeve.Description
    ( Description(..)
    , words'
    , layersAt
    , layerWords
    , newDescription
    , readWord
    , writeWord
    , readColour
    , writeColour
    , layerAt
    , kindOf
    , repeatingOf
    , stopCountOf
    , angleOf
    , cxOf
    , cyOf
    , exOf
    , eyOf
    , stopAt
    , stopColour
    ) where

import Data.Array.Byte          ( MutableByteArray(..) )
import Data.Word                ( Word32 )
import GHC.Exts                 ( Int(..), RealWorld, int32ToInt#, intToInt32#, newByteArray#, readInt32Array#
                                , readWord32Array#, word32ToWord#, wordToWord32#, writeInt32Array#
                                , writeWord32Array# )
import GHC.IO                   ( IO(..) )
import GHC.Word                 ( Word32(..) )


-- | Room for one description.
newtype Description = Description (MutableByteArray RealWorld)

-- | The words in a description, and where its layers start.
words', layersAt, layerWords :: Int
words'     = layersAt + 12 * layerWords
layersAt   = 11
layerWords = 24

newDescription :: IO Description
newDescription = IO $ \s -> case newByteArray# (unI (4 * words')) s of
  (# s1, m #) -> (# s1, Description (MutableByteArray m) #)
  where unI (I# i) = i

readWord :: Description -> Int -> IO Int
readWord (Description (MutableByteArray m)) (I# i) = IO $ \s -> case readInt32Array# m i s of
  (# s1, v #) -> (# s1, I# (int32ToInt# v) #)
{-# INLINE readWord #-}

writeWord :: Description -> Int -> Int -> IO ()
writeWord (Description (MutableByteArray m)) (I# i) (I# v) = IO $ \s ->
  (# writeInt32Array# m i (intToInt32# v) s, () #)
{-# INLINE writeWord #-}

readColour :: Description -> Int -> IO Word32
readColour (Description (MutableByteArray m)) (I# i) = IO $ \s -> case readWord32Array# m i s of
  (# s1, v #) -> (# s1, W32# v #)
{-# INLINE readColour #-}

writeColour :: Description -> Int -> Word32 -> IO ()
writeColour (Description (MutableByteArray m)) (I# i) (W32# v) = IO $ \s ->
  (# writeWord32Array# m i (wordToWord32# (word32ToWord# v)) s, () #)
{-# INLINE writeColour #-}

-- | The first word of layer @i@.
layerAt :: Int -> Int
layerAt i = layersAt + layerWords * i
{-# INLINE layerAt #-}

-- | Each field's word, from its layer's first.
kindOf, repeatingOf, stopCountOf, angleOf, cxOf, cyOf, exOf, eyOf :: Int -> Int
kindOf      l = l
repeatingOf l = l + 1
stopCountOf l = l + 2
angleOf     l = l + 3
cxOf        l = l + 4
cyOf        l = l + 5
exOf        l = l + 6
eyOf        l = l + 7

-- | The words of stop @j@'s position and colour.
stopAt, stopColour :: Int -> Int -> Int
stopAt     l j = l + 8 + 2 * j
stopColour l j = l + 9 + 2 * j
