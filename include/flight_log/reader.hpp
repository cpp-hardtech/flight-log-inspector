/****************************************************************************
 * See NOTICE for the copyright declaration for this file.
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/
#pragma once

#include <memory>
#include <set>

#include "data_handler_interface.hpp"

namespace flight_log {

/**
 * Class to deserialize an ULog file. Parsed messages are passed back to a DataHandlerInterface
 * class.
 */
class Reader {
 public:
  explicit Reader(std::shared_ptr<DataHandlerInterface> data_handler_interface);
  ~Reader();

  /**
   * Parse next chunk of serialized ULog data. Call this iteratively, e.g. over a complete file.
   * data_handler_interface will be called immediately for each parsed ULog message.
   */
  void readChunk(const uint8_t* data, int length);

  /**
   * Signal the end of the input, reporting an incomplete file header or final message.
   * If the log ends after complete definition records, this also completes the header.
   * Further reads and repeated calls to finish() are ignored.
   */
  void finish();

 private:
  static constexpr int kBufferSizeInit = 2048;

  static const std::set<ULogMessageType> kKnownMessageTypes;

  void readMagic(const ulog_file_header_s& header);
  void readFlagBits(const ulog_message_flag_bits_s& flag_bits);
  void corruptionDetected();
  int appendToPartialBuffer(const uint8_t* data, int length);
  void tryToRecover(const uint8_t*& data, int& length);

  void readHeaderMessage(const uint8_t* message);
  void readDataMessage(const uint8_t* message);

  enum class State {
    ReadMagic,
    ReadFlagBits,
    ReadHeader,
    ReadData,
    InvalidData,
    Finished,
  };

  State _state{State::ReadMagic};
  const std::shared_ptr<DataHandlerInterface> _data_handler_interface;

  uint8_t* _partial_message_buffer{nullptr};  ///< buffers incomplete input and recovery bytes
  int _partial_message_buffer_length_capacity{0};
  int _partial_message_buffer_length{0};

  bool _need_recovery{false};
  bool _corruption_reported{false};

  int _total_num_read{};  ///< statistics, total number of bytes read (includes current partial
                          ///< buffer data)

  ulog_file_header_s _file_header{};
};

}  // namespace flight_log
