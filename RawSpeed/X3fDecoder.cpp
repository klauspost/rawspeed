#include "StdAfx.h"
#include "X3fDecoder.h"
#include "ByteStreamSwap.h"
/*
RawSpeed - RAW file decoder.

Copyright (C) 2009-2014 Klaus Post

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

http://www.klauspost.com
*/

namespace RawSpeed {

X3fDecoder::X3fDecoder(FileMap* file) :
RawDecoder(file), bytes(NULL) {
  decoderVersion = 1;
  huge_table = NULL;
  line_offsets = NULL;
  if (getHostEndianness() == little)
    bytes = new ByteStream(file->getData(0), file->getSize());
  else
    bytes = new ByteStreamSwap(file->getData(0), file->getSize());
}

X3fDecoder::~X3fDecoder(void)
{
  if (bytes)
    delete bytes;
  if (huge_table)
    _aligned_free(huge_table);
  if (line_offsets)
    _aligned_free(line_offsets);
  huge_table = NULL;
  line_offsets = NULL;
}

string X3fDecoder::getIdAsString(ByteStream *bytes) {
  uchar8 id[5];
  for (int i = 0; i < 4; i++)
    id[i] = bytes->getByte();
  id[4] = 0;
  return string((const char*)id);
}


RawImage X3fDecoder::decodeRawInternal()
{
  vector<X3fImage>::iterator img = mImages.begin();
  for (; img !=  mImages.end(); img++) {
    X3fImage cimg = *img;
    if (cimg.type == 1 || cimg.type == 3) {
      decompressSigma(cimg);
      break;
    }
  }
  return mRaw;
}

void X3fDecoder::decodeMetaDataInternal( CameraMetaData *meta )
{
  if (hasProp("CAMMANUF") && hasProp("CAMMODEL")) {
    if (checkCameraSupported(meta, getProp("CAMMANUF"), getProp("CAMMODEL"), "" )) {
      int iso = 0;
      if (hasProp("ISO"))
        iso = atoi(getProp("ISO").c_str());
      setMetaData(meta, getProp("CAMMANUF"), getProp("CAMMODEL"), "", iso);
      return;
    }
  }
}

void X3fDecoder::checkSupportInternal( CameraMetaData *meta )
{
  if (hasProp("CAMMANUF") && hasProp("CAMMODEL")) {
    if (!checkCameraSupported(meta, getProp("CAMMANUF"), getProp("CAMMODEL"), "" ))
      ThrowRDE("X3FDecoder: Unknown camera. Will not guess.");
    return;
  }

  // If we somehow got to here without a camera, see if we have an image
  // with proper format identifiers.
  vector<X3fImage>::iterator img = mImages.begin();
  for (; img !=  mImages.end(); img++) {
    X3fImage cimg = *img;
    if (cimg.type == 1 || cimg.type == 3) {
      if (cimg.format == 30)
        return;
    }
  }
  ThrowRDE("X3F Decoder: Unable to determine camera name.");
}

string X3fDecoder::getProp(const char* key )
{
  map<string,string>::iterator prop_it = mProperties.props.find(key);
  if (prop_it != mProperties.props.end())
    return (*prop_it).second;
  return NULL;
}


void X3fDecoder::decompressSigma( X3fImage &image )
{
  ByteStream input(mFile->getDataWrt(image.dataOffset), image.dataSize);
  mRaw->dim.x = image.width;
  mRaw->dim.y = image.height;
  mRaw->setCpp(3);
  mRaw->isCFA = false;
  mRaw->createData();
  curr_image = &image;

  if (image.format == 30) {
    for (int i = 0; i < 3; i++)
      pred[i] = input.getShort();

    // Skip padding
    input.skipBytes(2);

    createSigmaTable(&input, 13);

    // Skip padding  (2 x 0x00)
    input.skipBytes(2);
    plane_offset[0] = image.dataOffset + 48;

    for (int i = 0; i < 3; i++) {
      plane_sizes[i] = input.getUInt();
      // Planes are 16 byte aligned
      if (i != 2) {
        plane_offset[i+1] = plane_offset[i] + (((plane_sizes[i] + 15) / 16) * 16);
        if (plane_offset[i]>mFile->getSize())
          ThrowRDE("SigmaDecompressor:Plane offset outside image");
      }
    }
    startTasks(3);
    return;
  } // End if format 30

  if (image.format == 6) {
    for (int i = 0; i < 1024; i++) {
      curve[i] = (short)input.getShort();
    }
    max_len = 0;
    uchar8 huff_len[1024];
    uint32 huff_code[1024];
    for (int i = 0; i < 1024; i++) {
      uint32 val = input.getUInt();
      huff_len[i] = val>>27;
      huff_code[i] = val&0x7ffffff;
      max_len = max(max_len, val>>27);
    }
    if (max_len>26)
      ThrowRDE("SigmaDecompressor: Codelength cannot be longer than 26, invalid data");

    //We create a HUGE table that contains all values up to the
    //maximum code length. Luckily values can only be up to 10
    //bits, so we can get away with using 2 bytes/value
    huge_table = (ushort16*)_aligned_malloc((1<<max_len)*2, 16);
    if (!huge_table)
      ThrowRDE("SigmaDecompressor: Memory Allocation failed.");

    memset(huge_table, 0xff, (1<<max_len)*2);
    for (int i = 0; i < 1024; i++) {
      if (huff_len[i]) {
        uint32 len = huff_len[i];
        uint32 code = huff_code[i]&((1<<len)-1);
        uint32 rem_bits = max_len-len;
        uint32 top_code = (code<<rem_bits);
        ushort16 store_val = (i << 5) | len;
        for (int j = 0; j < (1<<rem_bits); j++)
          huge_table[top_code|j] = store_val; 
      }
    }
    // Load offsets
    ByteStream i2(mFile->getDataWrt(image.dataOffset+image.dataSize-mRaw->dim.y*4), mRaw->dim.y*4);
    line_offsets = (uint32*)_aligned_malloc(4*mRaw->dim.y, 16);
    if (!line_offsets)
      ThrowRDE("SigmaDecompressor: Memory Allocation failed.");
    for (int y = 0; y < mRaw->dim.y; y++) {
      line_offsets[y] = i2.getUInt() + input.getOffset() + image.dataOffset;
    }
    startThreads();
    return;
  }
  ThrowRDE("X3fDecoder: Unable to find decoder for format: %d", image.format);
}



void X3fDecoder::createSigmaTable(ByteStream *bytes, int codes) {
  memset(code_table, 0xff, sizeof(code_table));

  // Read codes and create 8 bit table with all valid values.
  for (int i = 0; i < codes; i++) {
    uint32 len = bytes->getByte();
    uint32 code = bytes->getByte();
    if (len > 8)
      ThrowRDE("X3fDecoder: bit length longer than 8");
    uint32 rem_bits = 8-len;
    for (int j = 0; j < (1<<rem_bits); j++)
      code_table[code|j] = (i << 4) | len; 
  }
  // Create a 14 bit table that contains code length
  // AND value. This is enough to decode most images,
  // and will make most codes be returned with a single
  // lookup.
  // If the table value is 0xf, it is not possible to get a
  // value from 14 bits.
  for (int i = 0; i < (1<<14); i++) {
    uint32 top = i>>6;
    uchar8 val = code_table[top];
    if (val != 0xff) {
      uint32 code_bits = val&0xf;
      uint32 val_bits = val>>4;
      if (code_bits + val_bits < 14) {
        uint32 low_pos = 14-code_bits-val_bits;
        int v = (int)(i>>low_pos)&((1<<val_bits) - 1);
        if ((v & (1 << (val_bits - 1))) == 0)
          v -= (1 << val_bits) - 1;
        big_table[i] = (v<<8) | (code_bits+val_bits);
      } else {
        big_table[i] = 0xf;
      }
    } else {
      big_table[i] = 0xf;
    }
  }
  return;
}

void X3fDecoder::decodeThreaded( RawDecoderThread* t )
{
  if (curr_image->format == 30) {
    uint32 i = t->taskNo;
    if (i>3)
      ThrowRDE("X3fDecoder:Invalid plane:%u (internal error)", i);
    /* We have a weird prediction which is actually more appropriate for a CFA image */
    BitPumpMSB *bits = new BitPumpMSB(mFile->getData(plane_offset[i]), mFile->getSize()-plane_offset[i]);
    /* Initialize predictors */
    int pred_up[4];
    int pred_left[2];
    for (int j = 0; j < 4; j++)
      pred_up[j] = pred[i];

    for (int y = 0; y < mRaw->dim.y; y++) {
      ushort16* dst = (ushort16*)mRaw->getData(0,y) + i;
      int diff1= SigmaDecode(bits);
      int diff2 = SigmaDecode(bits);
      dst[0] = pred_left[0] = pred_up[y & 1] = pred_up[y & 1] + diff1;
      dst[3] = pred_left[1] = pred_up[(y & 1) + 2] = pred_up[(y & 1) + 2] + diff2;
      dst+=6;
      for (int x = 2; x < mRaw->dim.x; x+=2) {
        int diff1= SigmaDecode(bits);
        int diff2 = SigmaDecode(bits);
        dst[0] = pred_left[0] = pred_left[0] + diff1;
        dst[3] = pred_left[1] = pred_left[1] + diff2;
        dst+=6;
      }
    }
    return;
  }

  if (curr_image->format == 6) {
    int pred[3];
    for (uint32 y = t->start_y; y < t->end_y; y++) {
      BitPumpMSB bits(mFile->getData(line_offsets[y]),mFile->getSize()-line_offsets[y]);
      ushort16* dst = (ushort16*)mRaw->getData(0,y);
      pred[0] = pred[1] = pred[2] = 0;
      for (int x = 0; x < mRaw->dim.x; x++) {
        for (int i = 0; i < 3; i++) {
          ushort16 val = huge_table[bits.peekBits(max_len)];
          uchar8 nbits = val&31;
          if (val == 0xffff) {
            ThrowRDE("SigmaDecompressor: Invalid Huffman value. Image Corrupt");
          }
          bits.skipBitsNoFill(nbits);
          pred[i] += curve[(val>>5)];
          dst[0] = clampbits(pred[i], 16);
          dst++;
        }
      }
    }
    return;
  }
}

/* Returns a single value by reading the bitstream*/
int X3fDecoder::SigmaDecode(BitPumpMSB *bits) {

  bits->fill();
  uint32 code = bits->peekBitsNoFill(14);
  int32 bigv = big_table[code];
  if (bigv != 0xf) {
    bits->skipBitsNoFill(bigv&0xff);
    return bigv >> 8;
  }
  uchar8 val = code_table[code>>6];
  if (val == 0xff)
    ThrowRDE("X3fDecoder: Invalid Huffman code");

  uint32 code_bits = val&0xf;
  uint32 val_bits = val>>4;
  bits->skipBitsNoFill(code_bits);
  if (!val_bits)
    return 0;
  int v = bits->getBitsNoFill(val_bits);
  if ((v & (1 << (val_bits - 1))) == 0)
    v -= (1 << val_bits) - 1;

  return v;
}

FileMap* X3fDecoder::getCompressedData()
{
  vector<X3fImage>::iterator img = mImages.begin();
  for (; img !=  mImages.end(); img++) {
    X3fImage cimg = *img;
    if (cimg.type == 1 || cimg.type == 3) {
      return new FileMap(mFile->getDataWrt(cimg.dataOffset), cimg.dataSize);
    }
  }
  return NULL;
}

} // namespace RawSpeed
