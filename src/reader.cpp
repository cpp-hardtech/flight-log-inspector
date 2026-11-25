/****************************************************************************
 * See NOTICE for the copyright declaration for this file.
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/

#include <flight_log/reader.hpp>

#include <cstring>

#include <flight_log/raw_messages.hpp>

#if 0  // NOLINT (readability-avoid-unconditional-preprocessor-if)
#define DBG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DBG_PRINTF(...)
#endif

namespace flight_log {

namespace {
constexpr int kULogHeaderLength = sizeof(ulog_message_header_s);
}  // namespace

const std::set<ULogMessageType> Reader::kKnownMessageTypes{{
    ULogMessageType::FORMAT,
    ULogMessageType::DATA,
    ULogMessageType::INFO,
    ULogMessageType::INFO_MULTIPLE,
    ULogMessageType::PARAMETER,
    ULogMessageType::PARAMETER_DEFAULT,
    ULogMessageType::ADD_LOGGED_MSG,
    ULogMessageType::REMOVE_LOGGED_MSG,
    ULogMessageType::SYNC,
    ULogMessageType::DROPOUT,
    ULogMessageType::LOGGING,
    ULogMessageType::LOGGING_TAGGED,
    ULogMessageType::FLAG_BITS,
}};

// cppcheck-suppress [uninitMemberVar,unmatchedSuppression]
Reader::Reader(std::shared_ptr<DataHandlerInterface> data_handler_interface)
    : _data_handler_interface(std::move(data_handler_interface))
{
  // Reader assumes to run on little endian
  // TODO: use std::endian from C++20
  int num = 1;
  // cppcheck-suppress [knownConditionTrueFalse,unmatchedSuppression]
  if (*reinterpret_cast<char*>(&num) != 1) {
    _data_handler_interface->error("Reader requires little endian", false);
    _state = State::InvalidData;
  }
  _partial_message_buffer = static_cast<uint8_t*>(malloc(kBufferSizeInit));
  _partial_message_buffer_length_capacity = kBufferSizeInit;
}

Reader::~Reader()
{
  if (_partial_message_buffer) {
    free(_partial_message_buffer);
  }
}

void Reader::readChunk(const uint8_t* data, int length)
{
  if (_state == State::InvalidData || _state == State::Finished || length <= 0) {
    return;
  }

  auto ensure_enough_data_in_partial_buffer = [&](int required_data) {
    if (_partial_message_buffer_length < required_data && length > 0) {
      const int num_append = std::min(required_data - _partial_message_buffer_length, length);
      const int required_capacity = _partial_message_buffer_length + num_append;
      if (required_capacity > _partial_message_buffer_length_capacity) {
        _partial_message_buffer_length_capacity = required_capacity;
        _partial_message_buffer =
            static_cast<uint8_t*>(realloc(_partial_message_buffer, required_capacity));
        DBG_PRINTF("%i: resized partial buffer to %i\n", _total_num_read, required_capacity);
      }
      appendToPartialBuffer(data, num_append);
      data += num_append;
      length -= num_append;
      _total_num_read += num_append;
    }
    return _partial_message_buffer_length >= required_data;
  };

  if (_state == State::ReadMagic) {
    if (!ensure_enough_data_in_partial_buffer(sizeof(ulog_file_header_s))) {
      return;
    }
    readMagic(*reinterpret_cast<const ulog_file_header_s*>(_partial_message_buffer));
    _partial_message_buffer_length = 0;
  }

  if (_state == State::ReadFlagBits) {
    if (!ensure_enough_data_in_partial_buffer(kULogHeaderLength)) {
      return;
    }
    const auto* header = reinterpret_cast<const ulog_message_header_s*>(_partial_message_buffer);
    if (static_cast<ULogMessageType>(header->msg_type) == ULogMessageType::FLAG_BITS) {
      const int flag_length = header->msg_size + kULogHeaderLength;
      if (flag_length < static_cast<int>(sizeof(ulog_message_flag_bits_s))) {
        _data_handler_interface->error("Not enough data to read file flags", false);
        _state = State::InvalidData;
        return;
      }
      if (!ensure_enough_data_in_partial_buffer(flag_length)) {
        return;
      }
      readFlagBits(*reinterpret_cast<const ulog_message_flag_bits_s*>(_partial_message_buffer));
      _partial_message_buffer_length = 0;
    } else {
      _data_handler_interface->fileHeader(FileHeader{_file_header});
      _state = State::ReadHeader;
    }
  }

  if (_state == State::InvalidData) {
    return;
  }
  while (length > 0 || _partial_message_buffer_length >= kULogHeaderLength) {
    if (_need_recovery) {
      tryToRecover(data, length);
      if (_need_recovery) {
        return;
      }
    }
    // Try to get a full ulog message. There's 2 options:
    // - we have some partial data in the buffer. We need to append and use that buffer
    // - no partial data left. Use 'data' if it contains a full message
    const uint8_t* ulog_message = nullptr;
    bool clear_from_partial_message_buffer = false;
    if (_partial_message_buffer_length > 0) {
      if (!ensure_enough_data_in_partial_buffer(kULogHeaderLength)) {
        return;
      }
      const auto* header = reinterpret_cast<const ulog_message_header_s*>(_partial_message_buffer);
      if (!ensure_enough_data_in_partial_buffer(header->msg_size + kULogHeaderLength)) {
        return;
      }
      ulog_message = _partial_message_buffer;
      clear_from_partial_message_buffer = true;
    } else {
      int full_message_length = 0;
      if (length >= kULogHeaderLength) {
        const ulog_message_header_s* header = reinterpret_cast<const ulog_message_header_s*>(data);
        if (length >= header->msg_size + kULogHeaderLength) {
          full_message_length = header->msg_size + kULogHeaderLength;
        }
      }
      if (full_message_length > 0) {
        ulog_message = data;
        data += full_message_length;
        length -= full_message_length;
        _total_num_read += full_message_length;
      } else {
        // Not a full message in buffer -> add to partial buffer
        const int num_append = appendToPartialBuffer(data, length);
        data += num_append;
        length -= num_append;
        _total_num_read += num_append;
      }
    }

    if (ulog_message) {
      const ulog_message_header_s* header =
          reinterpret_cast<const ulog_message_header_s*>(ulog_message);

      // Check for corruption
      if (header->msg_size == 0 || header->msg_type == 0) {
        DBG_PRINTF("%i: Invalid msg detected\n", _total_num_read);
        corruptionDetected();
      } else {
        // Parse the message
        try {
          if (_state == State::ReadHeader) {
            readHeaderMessage(ulog_message);
          }
          if (_state == State::ReadData) {
            readDataMessage(ulog_message);
          }
        } catch (const ParsingException& exception) {
          DBG_PRINTF("%i: parser exception: %s\n", _total_num_read, exception.what());
          corruptionDetected();
        }
      }

      if (clear_from_partial_message_buffer) {
        // In most cases this will clear the whole buffer, but in case of corruptions we might have
        // more data
        const int num_remove = header->msg_size + kULogHeaderLength;
        memmove(_partial_message_buffer, _partial_message_buffer + num_remove,
                _partial_message_buffer_length - num_remove);
        _partial_message_buffer_length -= num_remove;
      }
    }
  }
}

void Reader::finish()
{
  const auto previous_state = _state;
  if (previous_state == State::InvalidData || previous_state == State::Finished) {
    return;
  }
  _state = State::Finished;

  if (previous_state == State::ReadMagic) {
    _data_handler_interface->error("Not enough data to read file magic", false);
  } else if (previous_state == State::ReadFlagBits &&
             _partial_message_buffer_length >= kULogHeaderLength) {
    _data_handler_interface->error("Not enough data to read file flags", false);
  } else {
    if (previous_state == State::ReadFlagBits) {
      _data_handler_interface->fileHeader(FileHeader{_file_header});
    }
    if (_partial_message_buffer_length > 0 && !_need_recovery) {
      _data_handler_interface->error("Truncated message at end of log", true);
    }
  }
  _partial_message_buffer_length = 0;
}

void Reader::tryToRecover(const uint8_t*& data, int& length)
{
  // Try to find a valid message in 'data' by moving data into the partial buffer and search for a
  // message
  while (length > 0 || _partial_message_buffer_length >= kULogHeaderLength) {
    if (length > 0) {
      const int num_append = appendToPartialBuffer(data, length);
      data += num_append;
      length -= num_append;
      _total_num_read += num_append;
    }

    if (_partial_message_buffer_length >= kULogHeaderLength) {
      bool found = false;
      int index = 0;
      for (; index <= _partial_message_buffer_length - kULogHeaderLength; ++index) {
        const ulog_message_header_s* header =
            reinterpret_cast<const ulog_message_header_s*>(_partial_message_buffer + index);
        // Try to use it if it looks sane (we could also check for a SYNC message)
        if (header->msg_size != 0 && header->msg_type != 0 && header->msg_size < 10000 &&
            kKnownMessageTypes.find(static_cast<ULogMessageType>(header->msg_type)) !=
                kKnownMessageTypes.end()) {
          // A DATA candidate additionally has to reference a real subscription with a
          // plausible payload size for it - the checks above alone (known type + size
          // cap) are satisfied by a lot of unrelated byte patterns, and DATA messages
          // are by far the most common type, so without this a resync can easily lock
          // onto the wrong offset: bytes that happen to look like a Data header for some
          // other message entirely, rather than the true next message. See
          // DataHandlerInterface::isValidDataMessage().
          bool candidate_valid = true;
          if (static_cast<ULogMessageType>(header->msg_type) == ULogMessageType::DATA) {
            if (header->msg_size < 2) {
              candidate_valid = false;
            } else if (_partial_message_buffer_length - index < kULogHeaderLength + 2) {
              // Keep the candidate until the rest of its message ID arrives.
              break;
            } else {
              uint16_t candidate_msg_id = 0;
              memcpy(&candidate_msg_id,
                     _partial_message_buffer + index + sizeof(ulog_message_header_s),
                     sizeof(candidate_msg_id));
              const auto candidate_payload_size = static_cast<uint16_t>(header->msg_size - 2);
              candidate_valid = _data_handler_interface->isValidDataMessage(candidate_msg_id,
                                                                            candidate_payload_size);
            }
          }
          if (candidate_valid) {
            found = true;
            break;
          }
        }
      }

      // Discard unused data
      if (index > 0) {
        memmove(_partial_message_buffer, _partial_message_buffer + index,
                _partial_message_buffer_length - index);
        _partial_message_buffer_length -= index;
      }

      if (found) {
        DBG_PRINTF(
            "%i: recovered (index = %i, length = %i, partial buf len = %i)\n",
            _total_num_read, index, length, _partial_message_buffer_length);
        _need_recovery = false;
        return;
      }
      DBG_PRINTF("%i: no valid msg found (length = %i, partial buf len = %i)\n", _total_num_read,
                 length, _partial_message_buffer_length);
    }
    if (length == 0) {
      return;
    }
  }
}

void Reader::corruptionDetected()
{
  if (!_corruption_reported) {
    _data_handler_interface->error("Message corruption detected", true);
    _corruption_reported = true;
  }
  _need_recovery = true;
}

int Reader::appendToPartialBuffer(const uint8_t* data, int length)
{
  const int num_append =
      std::min(length, _partial_message_buffer_length_capacity - _partial_message_buffer_length);
  memcpy(_partial_message_buffer + _partial_message_buffer_length, data, num_append);
  _partial_message_buffer_length += num_append;
  return num_append;
}

void Reader::readMagic(const ulog_file_header_s& header)
{
  // Check magic bytes
  if (memcmp(header.magic, ulog_file_magic_bytes, sizeof(ulog_file_magic_bytes)) != 0) {
    _data_handler_interface->error("Invalid file format (incorrect header bytes)", false);
    _state = State::InvalidData;
    return;
  }

  _state = State::ReadFlagBits;
  _file_header = header;
}

void Reader::readFlagBits(const ulog_message_flag_bits_s& flag_bits)
{
  if (flag_bits.appended_offsets[0] != 0) {
    // TODO: handle appended data
    _data_handler_interface->error("File contains appended offsets - this is not supported", true);
  }
  // Check incompat flags
  bool has_incompat_flags = flag_bits.incompat_flags[0] & ~ULOG_INCOMPAT_FLAG0_DATA_APPENDED_MASK;
  for (unsigned i = 1;
       i < sizeof(flag_bits.incompat_flags) / sizeof(flag_bits.incompat_flags[0]); ++i) {
    if (flag_bits.incompat_flags[i]) {
      has_incompat_flags = true;
    }
  }
  if (has_incompat_flags) {
    _data_handler_interface->error("Unknown incompatible flag set: cannot parse the log", false);
    _state = State::InvalidData;
  } else {
    _data_handler_interface->fileHeader({_file_header, flag_bits});
    _state = State::ReadHeader;
  }
}

void Reader::readHeaderMessage(const uint8_t* message)
{
  const ulog_message_header_s* header = reinterpret_cast<const ulog_message_header_s*>(message);
  switch (static_cast<ULogMessageType>(header->msg_type)) {
    case ULogMessageType::INFO:
      _data_handler_interface->messageInfo(MessageInfo{message, false});
      break;
    case ULogMessageType::INFO_MULTIPLE:
      _data_handler_interface->messageInfo(MessageInfo{message, true});
      break;
    case ULogMessageType::FORMAT:
      _data_handler_interface->messageFormat(MessageFormat{message});
      break;
    case ULogMessageType::PARAMETER:
      _data_handler_interface->parameter(Parameter{message});
      break;
    case ULogMessageType::PARAMETER_DEFAULT:
      _data_handler_interface->parameterDefault(ParameterDefault{message});
      break;
    case ULogMessageType::ADD_LOGGED_MSG:
    case ULogMessageType::LOGGING:
    case ULogMessageType::LOGGING_TAGGED:
      DBG_PRINTF("%i: Header completed\n", _total_num_read);
      _state = State::ReadData;
      _data_handler_interface->headerComplete();
      break;
    default:
      DBG_PRINTF("%i: Unknown/unexpected message type in header: %i\n", _total_num_read,
                 header->msg_size);
      break;
  }
}

void Reader::readDataMessage(const uint8_t* message)
{
  const ulog_message_header_s* header = reinterpret_cast<const ulog_message_header_s*>(message);
  switch (static_cast<ULogMessageType>(header->msg_type)) {
    case ULogMessageType::INFO:
      _data_handler_interface->messageInfo(MessageInfo{message, false});
      break;
    case ULogMessageType::INFO_MULTIPLE:
      _data_handler_interface->messageInfo(MessageInfo{message, true});
      break;
    case ULogMessageType::PARAMETER:
      _data_handler_interface->parameter(Parameter{message});
      break;
    case ULogMessageType::PARAMETER_DEFAULT:
      _data_handler_interface->parameterDefault(ParameterDefault{message});
      break;
    case ULogMessageType::ADD_LOGGED_MSG:
      _data_handler_interface->addLoggedMessage(AddLoggedMessage{message});
      break;
    case ULogMessageType::LOGGING:
      _data_handler_interface->logging(Logging{message});
      break;
    case ULogMessageType::LOGGING_TAGGED:
      _data_handler_interface->logging(Logging{message, true});
      break;
    case ULogMessageType::DATA:
      _data_handler_interface->data(Data{message});
      break;
    case ULogMessageType::DROPOUT:
      _data_handler_interface->dropout(Dropout{message});
      break;
    case ULogMessageType::SYNC:
      _data_handler_interface->sync(Sync{message});
      break;
    default:
      DBG_PRINTF("%i: Unknown/unexpected message type in data: %i\n", _total_num_read,
                 header->msg_size);
      break;
  }
}

}  // namespace flight_log
