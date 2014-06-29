#include "StdAfx.h"
#include "MrwDecoder.h"
/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2009 Klaus Post

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

MrwDecoder::MrwDecoder(FileMap* file) :
    RawDecoder(file) {
  parseHeader();
}

MrwDecoder::~MrwDecoder(void) {
}

int MrwDecoder::isMRW(FileMap* input) {
  const uchar8* data = input->getData(0);
  return data[0] == 0x00 && data[1] == 0x4D && data[2] == 0x52 && data[3] == 0x4D;
}

#define get2BE(data,pos) ((((ushort16)(data)[pos]) << 8) | ((ushort16)(data)[pos+1]))

#define get4BE(data,pos) ((((uint32)(data)[pos]) << 24) | (((uint32)(data)[pos+1]) << 16) | \
                          (((uint32)(data)[pos+2]) << 8) | ((uint32)(data)[pos+3]))

#define get8LE(data,pos) ((((uint64)(data)[pos+7]) << 56) | (((uint64)(data)[pos+6]) << 48) | \
                          (((uint64)(data)[pos+5]) << 40) | (((uint64)(data)[pos+4]) << 32) | \
                          (((uint64)(data)[pos+3]) << 24) | (((uint64)(data)[pos+2]) << 16) | \
                          (((uint64)(data)[pos+1]) << 8) | ((uint64)(data)[pos]))
                        
/* This table includes all cameras that have ever had official MRW raw support.
   There were also a few compacts (G400, G500, G530 and G600) that had a raw
   mode in a hidden menu with MRW format written to JPG named files. It should
   be easy to support them given example files but chances are it was more of a
   novelty than something people actually used. */
static mrw_camera_t mrw_camera_table[] = {
  {"27820001", "DIMAGE A1"},
  {"27200001", "DIMAGE A2"},
  {"27470002", "DIMAGE A200"},
  {"27730001", "DIMAGE 5"},
  {"27660001", "DIMAGE 7"},
  {"27790001", "DIMAGE 7I"},
  {"27780001", "DIMAGE 7HI"},
  {"21810002", "DYNAX 7D"},
  {"21860002", "DYNAX 5D"},
};

void MrwDecoder::parseHeader() {
  const unsigned char* data = mFile->getData(0);
  
  if (mFile->getSize() < 30)
    ThrowRDE("Not a valid MRW file (size too small)");

  if (!isMRW(mFile))
    ThrowRDE("This isn't actually a MRW file, why are you calling me?");
    
  data_offset = get4BE(data,4)+8;
  
  // Let's just get all we need from the PRD block and be done with it
  raw_height = get2BE(data,24);
  raw_width = get2BE(data,26);
  packed = (data[32] == 12);
  cameraid = get8LE(data,16);
  cameraName = modelName(cameraid);
  if (!cameraName) {
    uchar8 cameracode[9] = {0};
    *((uint64 *) cameracode) = cameraid;
    ThrowRDE("MRW decoder: Unknown camera with ID %s", cameracode);
  }
}

const char* MrwDecoder::modelName(uint64 cameraid) {
  for (uint32 i=0; i<sizeof(mrw_camera_table)/sizeof(mrw_camera_table[0]); i++) { 
    if (*((uint64*) mrw_camera_table[i].code) == cameraid) {
        return mrw_camera_table[i].name;
    }
  }
  return NULL;
}

RawImage MrwDecoder::decodeRawInternal() {
  uint32 imgsize;

  mRaw->dim = iPoint2D(raw_width, raw_height);
  mRaw->createData();

  if (packed)
    imgsize = raw_width * raw_height * 3 / 2;
  else
    imgsize = raw_width * raw_height * 2;

  if (!mFile->isValid(data_offset))
    ThrowRDE("MRW decoder: Data offset after EOF, file probably truncated");
  if (!mFile->isValid(data_offset+imgsize-1))
    ThrowRDE("MRW decoder: Image end after EOF, file probably truncated");

  ByteStream input(mFile->getData(data_offset), imgsize);
 
  try {
    if (packed)
      Decode12BitRawBE(input, raw_width, raw_height);
    else
      Decode12BitRawBEunpacked(input, raw_width, raw_height);
  } catch (IOException &e) {
    mRaw->setError(e.what());
    // Let's ignore it, it may have delivered somewhat useful data.
  }

  return mRaw;
}

void MrwDecoder::checkSupportInternal(CameraMetaData *meta) {
  this->checkCameraSupported(meta, "MINOLTA", cameraName, "");
}

void MrwDecoder::decodeMetaDataInternal(CameraMetaData *meta) {
  //Default
  int iso = 0;

  setMetaData(meta, "MINOLTA", cameraName, "", iso);
}

} // namespace RawSpeed
