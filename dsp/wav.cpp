//
//  wav.cpp
//  NeuralAmpModeler-macOS
//
//  Created by Steven Atkinson on 12/31/22.
//

#include <cstring> // strncmp
#include <cmath> // pow
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

#include "wav.h"

bool idIsNotJunk(char* id)
{
  return strncmp(id, "RIFF", 4) == 0 || strncmp(id, "WAVE", 4) == 0 || strncmp(id, "fmt ", 4) == 0
         || strncmp(id, "data", 4) == 0;
}

bool ReadChunkAndSkipJunk(std::ifstream& file, char* chunkID)
{
  file.read(chunkID, 4);
  while (!idIsNotJunk(chunkID) && file.good())
  {
    int junkSize;
    file.read(reinterpret_cast<char*>(&junkSize), 4);
    file.ignore(junkSize);
    // Unused byte if junkSize is odd
    if ((junkSize % 2) == 1)
      file.ignore(1);
    // And now we should be ready for data...
    file.read(chunkID, 4);
  }
  return file.good();
}

namespace
{
constexpr unsigned short AUDIO_FORMAT_PCM = 1;
constexpr unsigned short AUDIO_FORMAT_IEEE = 3;
constexpr unsigned short AUDIO_FORMAT_EXTENSIBLE = 0xFFFE;

// KSDATAFORMAT_SUBTYPE_PCM / KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
constexpr std::uint8_t SUBFORMAT_PCM_GUID[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                                  0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
constexpr std::uint8_t SUBFORMAT_IEEE_GUID[16] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                                   0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

inline bool _GuidMatches(const std::uint8_t* lhs, const std::uint8_t* rhs)
{
  return std::memcmp(lhs, rhs, 16) == 0;
}

void _LoadSamplesI16ToMono(std::ifstream& wavFile, const int chunkSize, const int numChannels, std::vector<float>& samples)
{
  const int bytesPerSample = 2;
  const int bytesPerFrame = bytesPerSample * numChannels;
  const int numFrames = chunkSize / bytesPerFrame;
  samples.resize(numFrames);
  const float invChannels = 1.0f / (float)numChannels;
  constexpr float scale = 1.0f / (float)(1 << 15);
  for (int frame = 0; frame < numFrames; ++frame)
  {
    float sum = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
      std::int16_t sample = 0;
      wavFile.read(reinterpret_cast<char*>(&sample), bytesPerSample);
      sum += (float)sample;
    }
    samples[frame] = sum * invChannels * scale;
  }
}

void _LoadSamplesI24ToMono(std::ifstream& wavFile, const int chunkSize, const int numChannels, std::vector<float>& samples)
{
  const int bytesPerSample = 3;
  const int bytesPerFrame = bytesPerSample * numChannels;
  const int numFrames = chunkSize / bytesPerFrame;
  samples.resize(numFrames);
  const float invChannels = 1.0f / (float)numChannels;
  constexpr float scale = 1.0f / (float)(1 << 23);
  for (int frame = 0; frame < numFrames; ++frame)
  {
    float sum = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
      sum += (float)dsp::wav::_ReadSigned24BitInt(wavFile);
    }
    samples[frame] = sum * invChannels * scale;
  }
}

void _LoadSamplesI32ToMono(std::ifstream& wavFile,
                           const int chunkSize,
                           const int numChannels,
                           const int validBitsPerSample,
                           std::vector<float>& samples)
{
  const int bytesPerSample = 4;
  const int bytesPerFrame = bytesPerSample * numChannels;
  const int numFrames = chunkSize / bytesPerFrame;
  samples.resize(numFrames);
  const float invChannels = 1.0f / (float)numChannels;
  const int shift = 32 - validBitsPerSample;
  const float scale = std::ldexp(1.0f, -(validBitsPerSample - 1));
  for (int frame = 0; frame < numFrames; ++frame)
  {
    float sum = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
      std::int32_t sample = 0;
      wavFile.read(reinterpret_cast<char*>(&sample), bytesPerSample);
      if (shift > 0)
        sample >>= shift;
      sum += (float)sample;
    }
    samples[frame] = sum * invChannels * scale;
  }
}

void _LoadSamplesF32ToMono(std::ifstream& wavFile, const int chunkSize, const int numChannels, std::vector<float>& samples)
{
  const int bytesPerSample = 4;
  const int bytesPerFrame = bytesPerSample * numChannels;
  const int numFrames = chunkSize / bytesPerFrame;
  samples.resize(numFrames);
  const float invChannels = 1.0f / (float)numChannels;
  for (int frame = 0; frame < numFrames; ++frame)
  {
    float sum = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
      float sample = 0.0f;
      wavFile.read(reinterpret_cast<char*>(&sample), bytesPerSample);
      sum += sample;
    }
    samples[frame] = sum * invChannels;
  }
}
} // namespace

std::string dsp::wav::GetMsgForLoadReturnCode(LoadReturnCode retCode)
{
  std::stringstream message;

  switch (retCode)
  {
    case (LoadReturnCode::ERROR_OPENING):
      message << "Failed to open file (is it being used by another "
                 "program?)";
      break;
    case (LoadReturnCode::ERROR_NOT_RIFF): message << "File is not a WAV file."; break;
    case (LoadReturnCode::ERROR_NOT_WAVE): message << "File is not a WAV file."; break;
    case (LoadReturnCode::ERROR_MISSING_FMT): message << "File is missing expected format chunk."; break;
    case (LoadReturnCode::ERROR_INVALID_FILE): message << "WAV file contents are invalid."; break;
    case (LoadReturnCode::ERROR_UNSUPPORTED_FORMAT_ALAW): message << "Unsupported file format \"A-law\""; break;
    case (LoadReturnCode::ERROR_UNSUPPORTED_FORMAT_MULAW): message << "Unsupported file format \"mu-law\""; break;
    case (LoadReturnCode::ERROR_UNSUPPORTED_FORMAT_EXTENSIBLE):
      message << "Unsupported file format \"extensible\"";
      break;
    case (LoadReturnCode::ERROR_NOT_MONO): message << "File is not mono."; break;
    case (LoadReturnCode::ERROR_UNSUPPORTED_BITS_PER_SAMPLE): message << "Unsupported bits per sample"; break;
    case (dsp::wav::LoadReturnCode::ERROR_OTHER): message << "???"; break;
    default: message << "???"; break;
  }

  return message.str();
}

dsp::wav::LoadReturnCode dsp::wav::Load(const char* fileName, std::vector<float>& audio, double& sampleRate)
{
  // FYI: https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html
  // Open the WAV file for reading
  std::ifstream wavFile(fileName, std::ios::binary);

  // Check if the file was opened successfully
  if (!wavFile.is_open())
  {
    std::cerr << "Error opening WAV file" << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_OPENING;
  }

  // WAV file has 3 "chunks": RIFF ("RIFF"), format ("fmt ") and data ("data").
  // Read the WAV file header
  char chunkId[4];
  if (!ReadChunkAndSkipJunk(wavFile, chunkId))
  {
    std::cerr << "Error while reading for next chunk." << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
  }

  if (strncmp(chunkId, "RIFF", 4) != 0)
  {
    std::cerr << "Error: File does not start with expected RIFF chunk. Got" << chunkId << " instead." << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_NOT_RIFF;
  }

  int chunkSize;
  wavFile.read(reinterpret_cast<char*>(&chunkSize), 4);

  char format[4];
  wavFile.read(format, 4);
  if (strncmp(format, "WAVE", 4) != 0)
  {
    std::cerr << "Error: Files' second chunk (format) is not expected WAV. Got" << format << " instead." << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_NOT_WAVE;
  }

  // Read the format chunk
  char subchunk1Id[4];
  if (!ReadChunkAndSkipJunk(wavFile, subchunk1Id))
  {
    std::cerr << "Error while reading for next chunk." << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
  }
  if (strncmp(subchunk1Id, "fmt ", 4) != 0)
  {
    std::cerr << "Error: Invalid WAV file missing expected fmt section; got " << subchunk1Id << " instead."
              << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_MISSING_FMT;
  }

  int subchunk1Size;
  wavFile.read(reinterpret_cast<char*>(&subchunk1Size), 4);
  if (subchunk1Size < 16)
  {
    std::cerr << "WAV chunk 1 size is " << subchunk1Size
              << ", which is smaller than the requried 16 to fit the expected "
                 "information."
              << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
  }

  unsigned short audioFormat;
  wavFile.read(reinterpret_cast<char*>(&audioFormat), 2);

  short numChannels;
  wavFile.read(reinterpret_cast<char*>(&numChannels), 2);
  if (numChannels < 1)
  {
    std::cerr << "Invalid channel count for WAV: " << numChannels << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
  }

  int iSampleRate;
  wavFile.read(reinterpret_cast<char*>(&iSampleRate), 4);
  // Store in format we assume (SR is double)
  sampleRate = (double)iSampleRate;

  int byteRate;
  wavFile.read(reinterpret_cast<char*>(&byteRate), 4);

  short blockAlign;
  wavFile.read(reinterpret_cast<char*>(&blockAlign), 2);

  short bitsPerSample;
  wavFile.read(reinterpret_cast<char*>(&bitsPerSample), 2);

  int validBitsPerSample = bitsPerSample;
  std::uint8_t subFormatGuid[16] = {0};
  bool hasExtensible = false;

  // The default is for there to be 16 bytes in the fmt chunk, but sometimes
  // it's different.
  if (subchunk1Size > 16)
  {
    if (subchunk1Size < 18)
      return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
    unsigned short cbSize = 0;
    wavFile.read(reinterpret_cast<char*>(&cbSize), 2);
    int consumedExtraBytes = 2;
    if (audioFormat == AUDIO_FORMAT_EXTENSIBLE)
    {
      if (cbSize < 22 || subchunk1Size < 40)
      {
        std::cerr << "Extensible WAV has insufficient fmt extra bytes." << std::endl;
        return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
      }
      unsigned short validBits = 0;
      wavFile.read(reinterpret_cast<char*>(&validBits), 2);
      validBitsPerSample = (int)validBits;
      int channelMask = 0;
      wavFile.read(reinterpret_cast<char*>(&channelMask), 4);
      wavFile.read(reinterpret_cast<char*>(subFormatGuid), 16);
      consumedExtraBytes += 22;
      hasExtensible = true;
    }

    const int fmtExtraBytes = subchunk1Size - 16;
    const int remaining = fmtExtraBytes - consumedExtraBytes;
    if (remaining > 0)
      wavFile.ignore(remaining);
  }

  if (audioFormat == AUDIO_FORMAT_EXTENSIBLE)
  {
    if (!hasExtensible)
      return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
    if (_GuidMatches(subFormatGuid, SUBFORMAT_PCM_GUID))
      audioFormat = AUDIO_FORMAT_PCM;
    else if (_GuidMatches(subFormatGuid, SUBFORMAT_IEEE_GUID))
      audioFormat = AUDIO_FORMAT_IEEE;
    else
    {
      std::cerr << "Unsupported extensible WAV subformat GUID." << std::endl;
      return dsp::wav::LoadReturnCode::ERROR_UNSUPPORTED_FORMAT_EXTENSIBLE;
    }
  }
  else if (audioFormat != AUDIO_FORMAT_PCM && audioFormat != AUDIO_FORMAT_IEEE)
  {
    std::cerr << "Error: Unsupported WAV format detected. ";
    switch (audioFormat)
    {
      case 6: std::cerr << "(Got: A-law)" << std::endl; return dsp::wav::LoadReturnCode::ERROR_UNSUPPORTED_FORMAT_ALAW;
      case 7:
        std::cerr << "(Got: mu-law)" << std::endl;
        return dsp::wav::LoadReturnCode::ERROR_UNSUPPORTED_FORMAT_MULAW;
      default:
        std::cerr << "(Got unknown format " << audioFormat << ")" << std::endl;
        return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
    }
  }

  if (audioFormat == AUDIO_FORMAT_PCM)
  {
    if (validBitsPerSample < 1 || validBitsPerSample > bitsPerSample)
    {
      std::cerr << "Invalid valid-bits-per-sample in WAV: " << validBitsPerSample << std::endl;
      return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
    }
    if (bitsPerSample == 16 && validBitsPerSample != 16)
      return dsp::wav::LoadReturnCode::ERROR_UNSUPPORTED_BITS_PER_SAMPLE;
    if (bitsPerSample == 24 && validBitsPerSample != 24)
      return dsp::wav::LoadReturnCode::ERROR_UNSUPPORTED_BITS_PER_SAMPLE;
    if (bitsPerSample == 32 && validBitsPerSample != 24 && validBitsPerSample != 32)
      return dsp::wav::LoadReturnCode::ERROR_UNSUPPORTED_BITS_PER_SAMPLE;
  }

  // Read the data chunk
  char subchunk2Id[4];
  if (!ReadChunkAndSkipJunk(wavFile, subchunk2Id))
  {
    std::cerr << "Error while reading for next chunk." << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
  }
  if (strncmp(subchunk2Id, "data", 4) != 0)
  {
    std::cerr << "Error: Invalid WAV file" << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
  }

  // Size of the data chunk, in bits.
  int subchunk2Size;
  wavFile.read(reinterpret_cast<char*>(&subchunk2Size), 4);
  if ((bitsPerSample % 8) != 0)
    return dsp::wav::LoadReturnCode::ERROR_UNSUPPORTED_BITS_PER_SAMPLE;
  const int bytesPerFrame = (bitsPerSample / 8) * numChannels;
  if (bytesPerFrame <= 0 || (subchunk2Size % bytesPerFrame) != 0)
  {
    std::cerr << "Invalid data chunk size for WAV format/channel layout." << std::endl;
    return dsp::wav::LoadReturnCode::ERROR_INVALID_FILE;
  }

  if (audioFormat == AUDIO_FORMAT_IEEE)
  {
    if (bitsPerSample == 32)
      _LoadSamplesF32ToMono(wavFile, subchunk2Size, numChannels, audio);
    else
    {
      std::cerr << "Error: Unsupported bits per sample for IEEE files: " << bitsPerSample << std::endl;
      return dsp::wav::LoadReturnCode::ERROR_UNSUPPORTED_BITS_PER_SAMPLE;
    }
  }
  else if (audioFormat == AUDIO_FORMAT_PCM)
  {
    if (bitsPerSample == 16)
      _LoadSamplesI16ToMono(wavFile, subchunk2Size, numChannels, audio);
    else if (bitsPerSample == 24)
      _LoadSamplesI24ToMono(wavFile, subchunk2Size, numChannels, audio);
    else if (bitsPerSample == 32)
      _LoadSamplesI32ToMono(wavFile, subchunk2Size, numChannels, validBitsPerSample, audio);
    else
    {
      std::cerr << "Error: Unsupported bits per sample for PCM files: " << bitsPerSample << std::endl;
      return dsp::wav::LoadReturnCode::ERROR_UNSUPPORTED_BITS_PER_SAMPLE;
    }
  }

  // Close the WAV file
  wavFile.close();

  // Print the number of samples
  // std::cout << "Number of samples: " << samples.size() << std::endl;

  return dsp::wav::LoadReturnCode::SUCCESS;
}

void dsp::wav::_LoadSamples16(std::ifstream& wavFile, const int chunkSize, std::vector<float>& samples)
{
  // Allocate an array to hold the samples
  std::vector<short> tmp(chunkSize / 2); // 16 bits (2 bytes) per sample

  // Read the samples from the file into the array
  wavFile.read(reinterpret_cast<char*>(tmp.data()), chunkSize);

  // Copy into the return array
  const float scale = 1.0 / ((double)(1 << 15));
  samples.resize(tmp.size());
  for (auto i = 0; i < samples.size(); i++)
    samples[i] = scale * ((float)tmp[i]); // 2^16
}

void dsp::wav::_LoadSamples24(std::ifstream& wavFile, const int chunkSize, std::vector<float>& samples)
{
  // Allocate an array to hold the samples
  std::vector<int> tmp(chunkSize / 3); // 24 bits (3 bytes) per sample
  // Read in and convert the samples
  for (int& x : tmp)
  {
    x = dsp::wav::_ReadSigned24BitInt(wavFile);
  }

  // Copy into the return array
  const float scale = 1.0 / ((double)(1 << 23));
  samples.resize(tmp.size());
  for (auto i = 0; i < samples.size(); i++)
    samples[i] = scale * ((float)tmp[i]);
}

int dsp::wav::_ReadSigned24BitInt(std::ifstream& stream)
{
  // Read the three bytes of the 24-bit integer.
  std::uint8_t bytes[3];
  stream.read(reinterpret_cast<char*>(bytes), 3);

  // Combine the three bytes into a single integer using bit shifting and
  // masking. This works by isolating each byte using a bit mask (0xff) and then
  // shifting the byte to the correct position in the final integer.
  int value = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);

  // The value is stored in two's complement format, so if the most significant
  // bit (the 24th bit) is set, then the value is negative. In this case, we
  // need to extend the sign bit to get the correct negative value.
  if (value & (1 << 23))
  {
    value |= ~((1 << 24) - 1);
  }

  return value;
}

void dsp::wav::_LoadSamples32(std::ifstream& wavFile, const int chunkSize, std::vector<float>& samples)
{
  // NOTE: 32-bit is float.
  samples.resize(chunkSize / 4); // 32 bits (4 bytes) per sample
  // Read the samples from the file into the array
  wavFile.read(reinterpret_cast<char*>(samples.data()), chunkSize);
}
