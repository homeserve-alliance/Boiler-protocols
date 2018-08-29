/*
 * ebusd - daemon for communication with eBUS heating systems.
 * Copyright (C) 2014-2018 John Baier <ebusd@ebusd.eu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/ebus/datatype.h"
#include <math.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstring>
#ifdef HAVE_CONTRIB
#  include "lib/ebus/contrib/contrib.h"
#endif

namespace ebusd {

using std::dec;
using std::hex;
using std::fixed;
using std::setfill;
using std::setprecision;
using std::setw;
using std::endl;


bool DataType::dump(bool asJson, size_t length, bool appendDivisor, ostream* output) const {
  if (asJson) {
    *output << "\"type\": \"" << m_id << "\"" << FIELD_SEPARATOR << " \"isbits\": "
            << (getBitCount() < 8 ? "true" : "false") << FIELD_SEPARATOR << " \"length\": ";
    if (isAdjustableLength() && length == REMAIN_LEN) {
      *output << "-1";
    } else {
      *output << static_cast<unsigned>(length);
    }
  } else {
    *output << m_id;
    if (isAdjustableLength()) {
      if (length == REMAIN_LEN) {
        *output << ":*";
      } else {
        *output << ":" << static_cast<unsigned>(length);
      }
    }
    if (appendDivisor) {
      *output << FIELD_SEPARATOR;
    }
  }
  return false;
}


result_t StringDataType::readRawValue(size_t offset, size_t length, const SymbolString& input,
    unsigned int* value) const {
  return RESULT_EMPTY;
}

result_t StringDataType::readSymbols(size_t offset, size_t length, const SymbolString& input,
    OutputFormat outputFormat, ostream* output) const {
  size_t start = 0, count = length;
  int incr = 1;
  symbol_t symbol;
  bool terminated = false;
  if (count == REMAIN_LEN && input.getDataSize() > offset) {
    count = input.getDataSize() - offset;
  } else if (offset + count > input.getDataSize()) {
    return RESULT_ERR_INVALID_POS;
  }
  if (hasFlag(REV)) {  // reverted binary representation (most significant byte first)
    start = length - 1;
    incr = -1;
  }

  if (outputFormat & OF_JSON) {
    *output << '"';
  }
  *output << setfill('0') << (m_isHex ? hex : dec);
  for (size_t index = start, i = 0; i < count; index += incr, i++) {
    symbol = input.dataAt(offset + index);
    if (m_isHex) {
      if (i > 0) {
        *output << ' ';
      }
      *output << setw(2) << static_cast<unsigned>(symbol);
    } else {
      if (symbol == 0x00) {
        terminated = true;
      } else if (!terminated) {
        if (symbol < 0x20) {
          symbol = (symbol_t)m_replacement;
        } else if (!isprint(symbol)) {
          symbol = '?';
        } else if (outputFormat & OF_JSON) {
          if (symbol == '"' || symbol == '\\') {
            *output << '\\';  // escape
          }
        }
        *output << static_cast<char>(symbol);
      }
    }
  }
  if (outputFormat & OF_JSON) {
    *output << '"';
  }
  return RESULT_OK;
}

result_t StringDataType::writeSymbols(size_t offset, size_t length, istringstream* input,
    SymbolString* output, size_t* usedLength) const {
  size_t start = 0, count = length;
  bool remainder = count == REMAIN_LEN && hasFlag(ADJ);
  int incr = 1;
  unsigned int value = 0;
  string token;

  if (hasFlag(REV)) {  // reverted binary representation (most significant byte first)
    start = length - 1;
    incr = -1;
  }
  if (isIgnored() && !hasFlag(REQ)) {
    if (remainder) {
      count = 1;
    }
    for (size_t index = start, i = 0; i < count; index += incr, i++) {
      output->dataAt(offset + index) = (symbol_t)m_replacement;  // fill up with replacement
    }
    if (usedLength != nullptr) {
      *usedLength = count;
    }
    return RESULT_OK;
  }
  result_t result;
  size_t i = 0, index;
  for (index = start; i < count; index += incr, i++) {
    if (m_isHex) {
      while (!input->eof() && input->peek() == ' ') {
        input->get();
      }
      if (input->eof()) {  // no more digits
        value = m_replacement;  // fill up with replacement
      } else {
        token.clear();
        token.push_back((symbol_t)input->get());
        if (input->eof()) {
          return RESULT_ERR_INVALID_NUM;  // too short hex value
        }
        token.push_back((symbol_t)input->get());
        if (input->eof()) {
          return RESULT_ERR_INVALID_NUM;  // too short hex value
        }
        value = parseInt(token.c_str(), 16, 0, 0xff, &result);
        if (result != RESULT_OK) {
          return result;  // invalid hex value
        }
      }
    } else {
      if (input->eof()) {
        value = m_replacement;
      } else {
        value = input->get();
        if (input->eof() || value < 0x20) {
          value = m_replacement;
        }
      }
    }
    if (remainder && input->eof() && i > 0) {
      if (value == 0x00 && !m_isHex) {
        output->dataAt(offset + index) = 0;
        index += incr;
      }
      break;
    }
    if (value > 0xff) {
      return RESULT_ERR_OUT_OF_RANGE;  // value out of range
    }
    output->dataAt(offset + index) = (symbol_t)value;
  }

  if (!remainder && i < count) {
    return RESULT_ERR_EOF;  // input too short
  }
  if (usedLength != nullptr) {
    *usedLength = (index-start)*incr;
  }
  return RESULT_OK;
}


result_t DateTimeDataType::readRawValue(size_t offset, size_t length, const SymbolString& input,
    unsigned int* value) const {
  return RESULT_EMPTY;
}

result_t DateTimeDataType::readSymbols(size_t offset, size_t length, const SymbolString& input,
    OutputFormat outputFormat, ostream* output) const {
  size_t start = 0, count = length;
  int incr = 1;
  symbol_t symbol, last = 0, hour = 0;
  if (count == REMAIN_LEN && input.getDataSize() > offset) {
    count = input.getDataSize() - offset;
  } else if (offset + count > input.getDataSize()) {
    return RESULT_ERR_INVALID_POS;
  }
  if (hasFlag(REV)) {  // reverted binary representation (most significant byte first)
    start = length - 1;
    incr = -1;
  }

  if (outputFormat & OF_JSON) {
    *output << '"';
  }
  int type = (m_hasDate?2:0) | (m_hasTime?1:0);
  for (size_t index = start, i = 0; i < count; index += incr, i++) {
    if (length == 4 && i == 2 && m_hasDate) {
      continue;  // skip weekday in between
    }
    symbol = input.dataAt(offset + index);
    if (hasFlag(BCD) && (hasFlag(REQ) || symbol != m_replacement)) {
      if ((symbol & 0xf0) > 0x90 || (symbol & 0x0f) > 0x09) {
        return RESULT_ERR_OUT_OF_RANGE;  // invalid BCD
      }
      symbol = (symbol_t)((symbol >> 4) * 10 + (symbol & 0x0f));
    }
    switch (type) {
    case 2:  // date only
      if (!hasFlag(REQ) && symbol == m_replacement) {
        if (i + 1 != length) {
          *output << nullptr_VALUE << ".";
          break;
        } else if (last == m_replacement) {
          if (length == 2) {  // number of days since 01.01.1900
            *output << nullptr_VALUE << ".";
          }
          *output << nullptr_VALUE;
          break;
        }
      }
      if (length == 2) {  // number of days since 01.01.1900
        if (i == 0) {
          break;
        }
        int mjd = last + symbol*256 + 15020;  // 01.01.1900
        int y = static_cast<int>((mjd-15078.2)/365.25);
        int m = static_cast<int>((mjd-14956.1-static_cast<int>(y*365.25))/30.6001);
        int d = mjd-14956-static_cast<int>(y*365.25)-static_cast<int>(m*30.6001);
        m--;
        if (m >= 13) {
          y++;
          m -= 12;
        }
        *output << dec << setfill('0') << setw(2) << static_cast<unsigned>(d) << "."
                << setw(2) << static_cast<unsigned>(m) << "." << static_cast<unsigned>(y + 1900);
        break;
      }
      if (i + 1 == length) {
        *output << (2000 + symbol);
      } else if (symbol < 1 || (i == 0 && symbol > 31) || (i == 1 && symbol > 12)) {
        return RESULT_ERR_OUT_OF_RANGE;  // invalid date
      } else {
        *output << setw(2) << dec << setfill('0') << static_cast<unsigned>(symbol) << ".";
      }
      break;

    case 1:  // time only
      if (!hasFlag(REQ) && symbol == m_replacement) {
        if (length == 1) {  // truncated time
          *output << nullptr_VALUE << ":" << nullptr_VALUE;
          break;
        }
        if (i > 0) {
          *output << ":";
        }
        *output << nullptr_VALUE;
        break;
      }
      if (hasFlag(SPE)) {  // minutes since midnight
        if (i == 0) {
          last = symbol;
          continue;
        }
        unsigned int minutes = symbol*256 + last;
        if (minutes > 24*60) {
          return RESULT_ERR_OUT_OF_RANGE;  // invalid value
        }
        unsigned int minutesHour = minutes / 60;
        if (minutesHour > 24) {
          return RESULT_ERR_OUT_OF_RANGE;  // invalid hour
        }
        *output << setw(2) << dec << setfill('0') << minutesHour;
        symbol = (symbol_t)(minutes % 60);
      } else if (length == 1) {  // truncated time
        if (m_bitCount < 8) {
          symbol = (symbol_t)(symbol & ((1 << m_bitCount) - 1));
        }
        if (i == 0) {
          symbol = (symbol_t)(symbol/(60/m_resolution));  // convert to hours
          index -= incr;  // repeat for minutes
          count++;
        } else {
          symbol = (symbol_t)((symbol % (60/m_resolution)) * m_resolution);  // convert to minutes
        }
      }
      if (i == 0) {
        if (symbol > 24) {
          return RESULT_ERR_OUT_OF_RANGE;  // invalid hour
        }
        hour = symbol;
      } else if (symbol > 59 || (hour == 24 && symbol > 0)) {
        return RESULT_ERR_OUT_OF_RANGE;  // invalid time
      }
      if (i > 0) {
        *output << ":";
      }
      *output << setw(2) << dec << setfill('0') << static_cast<unsigned>(symbol);
      break;
    }
    last = symbol;
  }
  if (outputFormat & OF_JSON) {
    *output << '"';
  }
  return RESULT_OK;
}

result_t DateTimeDataType::writeSymbols(size_t offset, size_t length, istringstream* input,
    SymbolString* output, size_t* usedLength) const {
  size_t start = 0, count = length;
  bool remainder = count == REMAIN_LEN && hasFlag(ADJ);
  int incr = 1;
  unsigned int value = 0, last = 0, lastLast = 0;
  string token;

  if (hasFlag(REV)) {  // reverted binary representation (most significant byte first)
    start = length - 1;
    incr = -1;
  }
  if (isIgnored() && !hasFlag(REQ)) {
    if (remainder) {
      count = 1;
    }
    for (size_t index = start, i = 0; i < count; index += incr, i++) {
      output->dataAt(offset + index) = (symbol_t)m_replacement;  // fill up with replacement
    }
    if (usedLength != nullptr) {
      *usedLength = count;
    }
    return RESULT_OK;
  }
  result_t result;
  size_t i = 0, index;
  int type = (m_hasDate?2:0) | (m_hasTime?1:0);
  bool skip = false;
  for (index = start; i < count; index += skip ? 0 : incr, i++) {
    skip = false;
    switch (type) {
    case 2:  // date only
      if (length == 4 && i == 2) {
        continue;  // skip weekday in between
      }
      if (input->eof() || !getline(*input, token, '.')) {
        return RESULT_ERR_EOF;  // incomplete
      }
      if (!hasFlag(REQ) && token == nullptr_VALUE) {
        value = m_replacement;
        break;
      }
      value = parseInt(token.c_str(), 10, 0, 2099, &result);
      if (result != RESULT_OK) {
        return result;  // invalid date part
      }
      if (length == 2) {  // number of days since 01.01.1900
        skip = true;
        if (i == 0) {
          count++;
        } else if (i + 1 == count) {
          int y = (value < 100 ? value + 2000 : value) - 1900;
          int l = last <= 2 ? 1 : 0;
          int mjd = 14956 + lastLast + static_cast<int>((y-l)*365.25) + static_cast<int>((last+1+l*12)*30.6001);
          value = mjd - 15020;  // 01.01.1900
          output->dataAt(offset + index) = (symbol_t)(value&0xff);
          value >>=  8;
          index += incr;
          skip = false;
          break;
        }
      }
      if (i + 1 == count) {
        if (length == 4) {
          // calculate local week day
          int y = (value < 100 ? value + 2000 : value) - 1900;
          int l = last <= 2 ? 1 : 0;
          int mjd = 14956 + lastLast + static_cast<int>((y-l)*365.25) + static_cast<int>((last+1+l*12)*30.6001);
          int daysSinceSunday = (mjd+3) % 7;  // Sun=0
          if (hasFlag(BCD)) {
            output->dataAt(offset + index - incr) = (symbol_t)((6+daysSinceSunday) % 7);  // Sun=0x06
          } else {
            // Sun=0x07
            output->dataAt(offset + index - incr) = (symbol_t)(daysSinceSunday == 0 ? 7 : daysSinceSunday);
          }
        }
        if (value >= 2000) {
          value -= 2000;
        }
        if (value > 99) {
          return RESULT_ERR_OUT_OF_RANGE;  // invalid year
        }
      } else if (value < 1 || (i == 0 && value > 31) || (i == 1 && value > 12)) {
        return RESULT_ERR_OUT_OF_RANGE;  // invalid date part
      }
      break;

    case 1:  // time only
      if (input->eof() || !getline(*input, token, LENGTH_SEPARATOR)) {
        return RESULT_ERR_EOF;  // incomplete
      }
      if (!hasFlag(REQ) && token == nullptr_VALUE) {
        value = m_replacement;
        if (length == 1) {  // truncated time
          if (i == 0) {
            skip = true;  // repeat for minutes
            count++;
            break;
          }
          if (last != m_replacement) {
            return RESULT_ERR_INVALID_NUM;  // invalid truncated time minutes
          }
        }
        break;
      }
      value = parseInt(token.c_str(), 10, 0, 59, &result);
      if (result != RESULT_OK) {
        return result;  // invalid time part
      }
      if ((i == 0 && value > 24) || (i > 0 && (last == 24 && value > 0) )) {
        return RESULT_ERR_OUT_OF_RANGE;  // invalid time part
      }
      if (hasFlag(SPE)) {  // minutes since midnight
        if (i == 0) {
          skip = true;  // repeat for minutes
          break;
        }
        value += last*60;
        output->dataAt(offset + index) = (symbol_t)(value&0xff);
        value >>=  8;
        index += incr;
      } else if (length == 1) {  // truncated time
        if (i == 0) {
          skip = true;  // repeat for minutes
          count++;
          break;
        }
        value = (last * 60 + value + m_resolution/2)/m_resolution;
        if (value > 24 * 6) {
          return RESULT_ERR_OUT_OF_RANGE;  // invalid time
        }
      }
      break;
    }
    lastLast = last;
    last = value;
    if (!skip) {
      if (hasFlag(BCD) && (hasFlag(REQ) || value != m_replacement)) {
        if (value > 99) {
          return RESULT_ERR_OUT_OF_RANGE;  // invalid BCD
        }
        value = ((value / 10) << 4) | (value % 10);
      }
      if (value > 0xff) {
        return RESULT_ERR_OUT_OF_RANGE;  // value out of range
      }
      output->dataAt(offset + index) = (symbol_t)value;
    }
  }

  if (!remainder && i < count) {
    return RESULT_ERR_EOF;  // input too short
  }
  if (usedLength != nullptr) {
    *usedLength = (index-start)*incr;
  }
  return RESULT_OK;
}


size_t NumberDataType::calcPrecision(int divisor) {
  size_t precision = 0;
  if (divisor > 1) {
    for (unsigned int exp = 1; exp < MAX_DIVISOR; exp *= 10, precision++) {
      if (exp >= (unsigned int)divisor) {
        break;
      }
    }
  }
  return precision;
}

bool NumberDataType::dump(bool asJson, size_t length, bool appendDivisor, ostream* output) const {
  if (m_bitCount < 8) {
    DataType::dump(asJson, m_bitCount, appendDivisor, output);
  } else {
    DataType::dump(asJson, length, appendDivisor, output);
  }
  if (!appendDivisor) {
    return false;
  }
  if (m_baseType) {
    if (m_baseType->m_divisor != m_divisor) {
      if (asJson) {
        *output << ", \"divisor\": ";
      }
      *output << (m_divisor / m_baseType->m_divisor);
      return true;
    }
  } else if (m_divisor != 1) {
    if (asJson) {
      *output << ", \"divisor\": ";
    }
    *output << m_divisor;
    return true;
  }
  return false;
}

result_t NumberDataType::derive(int divisor, size_t bitCount, const NumberDataType** derived) const {
  if (divisor == 0) {
    divisor = 1;
  }
  if (m_divisor != 1) {
    if (divisor == 1) {
      divisor = m_divisor;
    } else if (divisor < 0) {
      if (m_divisor > 1) {
        return RESULT_ERR_INVALID_ARG;
      }
      divisor *= -m_divisor;
    } else if (m_divisor < 0) {
      if (divisor > 1) {
        return RESULT_ERR_INVALID_ARG;
      }
      divisor *= -m_divisor;
    } else {
      divisor *= m_divisor;
    }
  }
  if (divisor == m_divisor && bitCount == m_bitCount) {
    *derived = this;
    return RESULT_OK;
  }
  if (-MAX_DIVISOR > divisor || divisor > MAX_DIVISOR) {
    return RESULT_ERR_OUT_OF_RANGE;
  }
  if (bitCount <= 0 || bitCount == m_bitCount) {
    bitCount = m_bitCount;
  } else if (isAdjustableLength()) {
    if (m_bitCount < 8) {
      if (bitCount+m_firstBit > 8) {
        return RESULT_ERR_OUT_OF_RANGE;
      }
    } else if ((bitCount%8) == 0) {
      return RESULT_ERR_INVALID_ARG;
    }
  } else {
    return RESULT_ERR_INVALID_ARG;
  }
  if (m_bitCount < 8) {
    *derived = new NumberDataType(m_id, bitCount, m_flags, m_replacement,
      m_firstBit, divisor, m_baseType ? m_baseType : this);
  } else {
    *derived = new NumberDataType(m_id, bitCount, m_flags, m_replacement,
      m_minValue, m_maxValue, divisor, m_baseType ? m_baseType : this);
  }
  DataTypeList::getInstance()->addCleanup(*derived);
  return RESULT_OK;
}

result_t NumberDataType::readRawValue(size_t offset, size_t length, const SymbolString& input,
    unsigned int* value) const {
  size_t start = 0, count = length;
  int incr = 1;
  symbol_t symbol;

  if (offset + length > input.getDataSize()) {
    return RESULT_ERR_INVALID_POS;  // not enough data available
  }
  if (hasFlag(REV)) {  // reverted binary representation (most significant byte first)
    start = length - 1;
    incr = -1;
  }

  *value = 0;
  unsigned int exp = 1;
  for (size_t index = start, i = 0; i < count; index += incr, i++) {
    symbol = input.dataAt(offset + index);
    if (hasFlag(BCD)) {
      if (!hasFlag(REQ) && symbol == (m_replacement & 0xff)) {
        *value = m_replacement;
        return RESULT_OK;
      }
      if (!hasFlag(HCD)) {
        if ((symbol & 0xf0) > 0x90 || (symbol & 0x0f) > 0x09) {
          return RESULT_ERR_OUT_OF_RANGE;  // invalid BCD
        }
        symbol = (symbol_t)((symbol >> 4) * 10 + (symbol & 0x0f));
      } else if (symbol > 0x63) {
        return RESULT_ERR_OUT_OF_RANGE;  // invalid HCD
      }
      *value += symbol * exp;
      exp *= 100;
    } else {
      *value |= symbol * exp;
      exp <<=  8;
    }
  }
  if (m_firstBit > 0) {
    *value >>=  m_firstBit;
  }
  if (m_bitCount < 8) {
    *value &= (1 << m_bitCount) - 1;
  }

  return RESULT_OK;
}

result_t NumberDataType::readSymbols(size_t offset, size_t length, const SymbolString& input,
    OutputFormat outputFormat, ostream* output) const {
  unsigned int value = 0;
  int signedValue;

  result_t result = readRawValue(offset, length, input, &value);
  if (result != RESULT_OK) {
    return result;
  }
  *output << setw(0) << dec;  // initialize output

  if (!hasFlag(REQ) && value == m_replacement) {
    if (outputFormat & OF_JSON) {
      *output << "null";
    } else {
      *output << nullptr_VALUE;
    }
    return RESULT_OK;
  }

  bool negative;
  if (hasFlag(SIG)) {  // signed value
    negative = (value & (1 << (m_bitCount - 1))) != 0;
    if (negative) {  // negative signed value
      if (value < m_minValue) {
        return RESULT_ERR_OUT_OF_RANGE;  // value out of range
      }
    } else if (value > m_maxValue) {
      return RESULT_ERR_OUT_OF_RANGE;  // value out of range
    }
  } else if (value < m_minValue || value > m_maxValue) {
    return RESULT_ERR_OUT_OF_RANGE;  // value out of range
  } else {
    negative = false;
  }
  if (m_bitCount == 32) {
    if (hasFlag(EXP)) {  // IEEE 754 binary32
      float val;
#ifdef HAVE_DIRECT_FLOAT_FORMAT
#  if HAVE_DIRECT_FLOAT_FORMAT == 2
      value = __builtin_bswap32(value);
#  endif
      symbol_t* pval = reinterpret_cast<symbol_t*>(&value);
      val = *reinterpret_cast<float*>(pval);
#else
      int exp = (value >> 23) & 0xff;  // 8 bits, signed
      if (exp == 0) {
        val = 0.0;
      } else {
        exp -= 127;
        unsigned int sig = value & ((1 << 23) - 1);
        val = (1.0f + static_cast<float>(sig / exp2(23))) * static_cast<float>(exp2(exp));
        if (negative) {
          val = -val;
        }
      }
#endif
      if (val != 0.0) {
        if (m_divisor < 0) {
          val *= static_cast<float>(-m_divisor);
        } else if (m_divisor > 1) {
          val /= static_cast<float>(m_divisor);
        }
      }
      if (m_precision != 0) {
        *output << fixed << setprecision(static_cast<int>(m_precision+6));
      } else if (val == 0) {
        *output << fixed << setprecision(1);
      }
      *output << static_cast<double>(val);
      return RESULT_OK;
    }
    if (!negative) {
      if (m_divisor < 0) {
        *output << (static_cast<float>(value) * static_cast<float>(-m_divisor));
      } else if (m_divisor <= 1) {
        *output << value;
      } else {
        *output << setprecision(static_cast<int>(m_precision))
                << fixed << (static_cast<float>(value) / static_cast<float>(m_divisor));
      }
      return RESULT_OK;
    }
    signedValue = static_cast<int>(value);  // negative signed value
  } else if (negative) {  // negative signed value
    signedValue = static_cast<int>(value) - (1 << m_bitCount);
  } else {
    signedValue = static_cast<int>(value);
  }
  if (m_divisor < 0) {
    *output << fixed << setprecision(0)
        << (static_cast<float>(signedValue) * static_cast<float>(-m_divisor));
  } else if (m_divisor <= 1) {
    if (hasFlag(FIX) && hasFlag(BCD)) {
      if (outputFormat & OF_JSON) {
        *output << '"' << setw(static_cast<int>(length * 2))
                << setfill('0') << signedValue << setw(0) << '"';
        return RESULT_OK;
      }
      *output << setw(static_cast<int>(length * 2)) << setfill('0');
    }
    *output << signedValue << setw(0);
  } else {
    *output << setprecision(static_cast<int>(m_precision))
            << fixed << (static_cast<float>(signedValue) / static_cast<float>(m_divisor));
  }
  return RESULT_OK;
}

result_t NumberDataType::writeRawValue(unsigned int value, size_t offset, size_t length,
    SymbolString* output, size_t* usedLength) const {
  size_t start = 0, count = length;
  int incr = 1;
  symbol_t symbol;

  if (m_bitCount < 8 && (value & ~((1 << m_bitCount) - 1)) != 0) {
    return RESULT_ERR_OUT_OF_RANGE;
  }
  if (m_firstBit > 0) {
    value <<=  m_firstBit;
  }

  if (hasFlag(REV)) {  // reverted binary representation (most significant byte first)
    start = length - 1;
    incr = -1;
  }

  for (size_t index = start, i = 0, exp = 1; i < count; index += incr, i++) {
    if (hasFlag(BCD)) {
      if (!hasFlag(REQ) && value == m_replacement) {
        symbol = m_replacement & 0xff;
      } else {
        symbol = (symbol_t)((value / exp) % 100);
        if (!hasFlag(HCD)) {
          symbol = (symbol_t)(((symbol / 10) << 4) | (symbol % 10));
        }
      }
      exp *= 100;
    } else {
      symbol = (value / exp) & 0xff;
      exp <<=  8;
    }
    if (index == start && (m_bitCount % 8) != 0 && offset + index < output->getDataSize()) {
      output->dataAt(offset + index) |= symbol;
    } else {
      output->dataAt(offset + index) = symbol;
    }
  }
  if (usedLength != nullptr) {
    *usedLength = length;
  }
  return RESULT_OK;
}

result_t NumberDataType::writeSymbols(size_t offset, size_t length, istringstream* input,
    SymbolString* output, size_t* usedLength) const {
  unsigned int value;

  if (!hasFlag(REQ) && (isIgnored() || input->str() == nullptr_VALUE)) {
    value = m_replacement;  // replacement value
  } else if (input->str().empty()) {
    return RESULT_ERR_EOF;  // input too short
  } else if (hasFlag(EXP)) {  // IEEE 754 binary32
    const char* str = input->str().c_str();
    char* strEnd = nullptr;
    double dvalue = strtod(str, &strEnd);
    if (strEnd == nullptr || strEnd == str || *strEnd != 0) {
      return RESULT_ERR_INVALID_NUM;  // invalid value
    }
    if (m_divisor < 0) {
      dvalue /= -m_divisor;
    } else if (m_divisor > 1) {
      dvalue *= m_divisor;
    }
#ifdef HAVE_DIRECT_FLOAT_FORMAT
    float val = static_cast<float>(dvalue);
    symbol_t* pval = reinterpret_cast<symbol_t*>(&val);
    value = *reinterpret_cast<int32_t*>(pval);
#  if HAVE_DIRECT_FLOAT_FORMAT == 2
    value = __builtin_bswap32(value);
#  endif
#else
    value = 0;
    if (dvalue != 0) {
      bool negative = dvalue < 0;
      if (negative) {
        dvalue = -dvalue;
      }
      int exp = ilogb(dvalue);
      if (exp < -126 || exp > 127) {
        return RESULT_ERR_INVALID_NUM;  // invalid value
      }
      dvalue = scalbln(dvalue, -exp) - 1.0;
      unsigned int sig = (unsigned int)(dvalue * exp2(23));
      exp += 127;
      value = (exp << 23) | sig;
      if (negative) {
        value |= 0x80000000;
      }
    }
#endif
  } else {
    const char* str = input->str().c_str();
    char* strEnd = nullptr;
    if (m_divisor == 1) {
      if (hasFlag(SIG)) {
        long signedValue = strtol(str, &strEnd, 10);
        if (signedValue < 0 && m_bitCount != 32) {
          value = (unsigned int)(signedValue + (1 << m_bitCount));
        } else {
          value = (unsigned int)signedValue;
        }
      } else {
        value = (unsigned int)strtoul(str, &strEnd, 10);
      }
      if (strEnd == nullptr || strEnd == str || (*strEnd != 0 && *strEnd != '.')) {
        return RESULT_ERR_INVALID_NUM;  // invalid value
      }
    } else {
      double dvalue = strtod(str, &strEnd);
      if (strEnd == nullptr || strEnd == str || *strEnd != 0) {
        return RESULT_ERR_INVALID_NUM;  // invalid value
      }
      if (m_divisor < 0) {
        dvalue = round(dvalue / -m_divisor);
      } else {
        dvalue = round(dvalue * m_divisor);
      }
      if (hasFlag(SIG)) {
        if (dvalue < -exp2((8 * static_cast<double>(length)) - 1)
            || dvalue >= exp2((8 * static_cast<double>(length)) - 1)) {
          return RESULT_ERR_OUT_OF_RANGE;  // value out of range
        }
        if (dvalue < 0 && m_bitCount != 32) {
          value = static_cast<int>(dvalue + (1 << m_bitCount));
        } else {
          value = static_cast<int>(dvalue);
        }
      } else {
        if (dvalue < 0.0 || dvalue >= exp2(8 * static_cast<double>(length))) {
          return RESULT_ERR_OUT_OF_RANGE;  // value out of range
        }
        value = (unsigned int)dvalue;
      }
    }

    if (hasFlag(SIG)) {  // signed value
      if ((value & (1 << (m_bitCount - 1))) != 0) {  // negative signed value
        if (value < m_minValue) {
          return RESULT_ERR_OUT_OF_RANGE;  // value out of range
        }
      } else if (value > m_maxValue) {
        return RESULT_ERR_OUT_OF_RANGE;  // value out of range
      }
    } else if (value < m_minValue || value > m_maxValue) {
      return RESULT_ERR_OUT_OF_RANGE;  // value out of range
    }
  }

  return writeRawValue(value, offset, length, output, usedLength);
}


DataTypeList DataTypeList::s_instance;

#ifdef HAVE_CONTRIB
bool DataTypeList::s_contrib_initialized = libebus_contrib_register();
#endif


DataTypeList::DataTypeList() {
  add(new StringDataType("STR", MAX_LEN*8, ADJ, ' '));  // >= 1 byte character string filled up with space
  // unsigned decimal in BCD, 0000 - 9999 (fixed length)
  add(new NumberDataType("PIN", 16, FIX|BCD|REV, 0xffff, 0, 0x9999, 1));
  add(new NumberDataType("UCH", 8, 0, 0xff, 0, 0xfe, 1));  // unsigned integer, 0 - 254
  add(new StringDataType("IGN", MAX_LEN*8, IGN|ADJ, 0));  // >= 1 byte ignored data
  // >= 1 byte character string filled up with 0x00 (null terminated string)
  add(new StringDataType("NTS", MAX_LEN*8, ADJ, 0));
  // >= 1 byte hex digit string, usually separated by space, e.g. 0a 1b 2c 3d
  add(new StringDataType("HEX", MAX_LEN*8, ADJ, 0, true));
  // date with weekday in BCD, 01.01.2000 - 31.12.2099 (0x01,0x01,WW,0x00 - 0x31,0x12,WW,0x99,
  // WW is weekday Mon=0x00 - Sun=0x06, replacement 0xff)
  add(new DateTimeDataType("BDA", 32, BCD, 0xff, true, false, 0));
  // date in BCD, 01.01.2000 - 31.12.2099 (0x01,0x01,0x00 - 0x31,0x12,0x99, replacement 0xff)
  add(new DateTimeDataType("BDA", 24, BCD, 0xff, true, false, 0));
  // date with weekday, 01.01.2000 - 31.12.2099 (0x01,0x01,WW,0x00 - 0x1f,0x0c,WW,0x63,
  // WW is weekday Mon=0x01 - Sun=0x07, replacement 0xff)
  add(new DateTimeDataType("HDA", 32, 0, 0xff, true, false, 0));
  // date, 01.01.2000 - 31.12.2099 (0x01,0x01,0x00 - 0x1f,0x0c,0x63, replacement 0xff)
  add(new DateTimeDataType("HDA", 24, 0, 0xff, true, false, 0));
  // date, days since 01.01.1900, 01.01.1900 - 06.06.2079 (0x00,0x00 - 0xff,0xff)
  add(new DateTimeDataType("DAY", 16, 0, 0xff, true, false, 0));
  // time in BCD, 00:00:00 - 23:59:59 (0x00,0x00,0x00 - 0x59,0x59,0x23)
  add(new DateTimeDataType("BTI", 24, BCD|REV, 0xff, false, true, 0));
  // time, 00:00:00 - 23:59:59 (0x00,0x00,0x00 - 0x17,0x3b,0x3b)
  add(new DateTimeDataType("HTI", 24, 0, 0xff, false, true, 0));
  // time, 00:00:00 - 23:59:59 (0x00,0x00,0x00 - 0x3b,0x3b,0x17, replacement 0x63) [Vaillant type]
  add(new DateTimeDataType("VTI", 24, REV, 0x63, false, true, 0));
  // time as hh:mm in BCD, 00:00 - 23:59 (0x00,0x00 - 0x59,0x23, replacement 0xff)
  add(new DateTimeDataType("BTM", 16, BCD|REV, 0xff, false, true, 0));
  // time as hh:mm, 00:00 - 23:59 (0x00,0x00 - 0x17,0x3b)
  add(new DateTimeDataType("HTM", 16, 0, 0xff, false, true, 0));
  // time as hh:mm, 00:00 - 23:59 (0x00,0x00 - 0x3b,0x17, replacement 0xff) [Vaillant type]
  add(new DateTimeDataType("VTM", 16, REV, 0xff, false, true, 0));
  // time, minutes since last midnight, 00:00 - 24:00 (minutes + hour * 60 as integer)
  add(new DateTimeDataType("MIN", 16, SPE, 0xff, false, true, 0));
  // truncated time (only multiple of 10 minutes), 00:00 - 24:00 (minutes div 10 + hour * 6 as integer)
  add(new DateTimeDataType("TTM", 8, 0, 0x90, false, true, 10));
  // truncated time (only multiple of 30 minutes), 00:00 - 24:00 (minutes div 30 + hour * 2 as integer)
  add(new DateTimeDataType("TTH", 6, 0, 0, false, true, 30));
  // truncated time (only multiple of 15 minutes), 00:00 - 24:00 (minutes div 15 + hour * 4 as integer)
  add(new DateTimeDataType("TTQ", 7, 0, 0, false, true, 15));
  add(new NumberDataType("BDY", 8, DAY, 0x07, 0, 6, 1));  // weekday, "Mon" - "Sun" (0x00 - 0x06) [eBUS type]
  add(new NumberDataType("HDY", 8, DAY, 0x00, 1, 7, 1));  // weekday, "Mon" - "Sun" (0x01 - 0x07) [Vaillant type]
  add(new NumberDataType("BCD", 8, BCD, 0xff, 0, 99, 1));  // unsigned decimal in BCD, 0 - 99
  add(new NumberDataType("BCD", 16, BCD, 0xffff, 0, 9999, 1));  // unsigned decimal in BCD, 0 - 9999
  add(new NumberDataType("BCD", 24, BCD, 0xffffff, 0, 999999, 1));  // unsigned decimal in BCD, 0 - 999999
  add(new NumberDataType("BCD", 32, BCD, 0xffffffff, 0, 99999999, 1));  // unsigned decimal in BCD, 0 - 99999999
  add(new NumberDataType("HCD", 32, HCD|BCD|REQ, 0, 0, 99999999, 1));  // unsigned decimal in HCD, 0 - 99999999
  add(new NumberDataType("HCD", 8, HCD|BCD|REQ, 0, 0, 99, 1));  // unsigned decimal in HCD, 0 - 99
  add(new NumberDataType("HCD", 16, HCD|BCD|REQ, 0, 0, 9999, 1));  // unsigned decimal in HCD, 0 - 9999
  add(new NumberDataType("HCD", 24, HCD|BCD|REQ, 0, 0, 999999, 1));  // unsigned decimal in HCD, 0 - 999999
  add(new NumberDataType("SCH", 8, SIG, 0x80, 0x81, 0x7f, 1));  // signed integer, -127 - +127
  add(new NumberDataType("D1B", 8, SIG, 0x80, 0x81, 0x7f, 1));  // signed integer, -127 - +127
  // unsigned number (fraction 1/2), 0 - 100 (0x00 - 0xc8, replacement 0xff)
  add(new NumberDataType("D1C", 8, 0, 0xff, 0x00, 0xc8, 2));
  // signed number (fraction 1/256), -127.99 - +127.99
  add(new NumberDataType("D2B", 16, SIG, 0x8000, 0x8001, 0x7fff, 256));
  // signed number (fraction 1/16), -2047.9 - +2047.9
  add(new NumberDataType("D2C", 16, SIG, 0x8000, 0x8001, 0x7fff, 16));
  // signed number (fraction 1/1000), -32.767 - +32.767, little endian
  add(new NumberDataType("FLT", 16, SIG, 0x8000, 0x8001, 0x7fff, 1000));
  // signed number (fraction 1/1000), -32.767 - +32.767, big endian
  add(new NumberDataType("FLR", 16, SIG|REV, 0x8000, 0x8001, 0x7fff, 1000));
  // signed number (IEEE 754 binary32: 1 bit sign, 8 bits exponent, 23 bits significand), little endian
  add(new NumberDataType("EXP", 32, SIG|EXP, 0x7f800000, 0x00000000, 0xffffffff, 1));
  // signed number (IEEE 754 binary32: 1 bit sign, 8 bits exponent, 23 bits significand), big endian
  add(new NumberDataType("EXR", 32, SIG|EXP|REV, 0x7f800000, 0x00000000, 0xffffffff, 1));
  // unsigned integer, 0 - 65534, little endian
  add(new NumberDataType("UIN", 16, 0, 0xffff, 0, 0xfffe, 1));
  // unsigned integer, 0 - 65534, big endian
  add(new NumberDataType("UIR", 16, REV, 0xffff, 0, 0xfffe, 1));
  // signed integer, -32767 - +32767, little endian
  add(new NumberDataType("SIN", 16, SIG, 0x8000, 0x8001, 0x7fff, 1));
  // signed integer, -32767 - +32767, big endian
  add(new NumberDataType("SIR", 16, SIG|REV, 0x8000, 0x8001, 0x7fff, 1));
  // unsigned 3 bytes int, 0 - 16777214, little endian
  add(new NumberDataType("U3N", 24, 0, 0xffffff, 0, 0xfffffe, 1));
  // unsigned 3 bytes int, 0 - 16777214, big endian
  add(new NumberDataType("U3R", 24, REV, 0xffffff, 0, 0xfffffe, 1));
  // signed 3 bytes int, -8388607 - +8388607, little endian
  add(new NumberDataType("S3N", 24, SIG, 0x800000, 0x800001, 0xffffff, 1));
  // signed 3 bytes int, -8388607 - +8388607, big endian
  add(new NumberDataType("S3R", 24, SIG|REV, 0x800000, 0x800001, 0xffffff, 1));
  // unsigned integer, 0 - 4294967294, little endian
  add(new NumberDataType("ULG", 32, 0, 0xffffffff, 0, 0xfffffffe, 1));
  // unsigned integer, 0 - 4294967294, big endian
  add(new NumberDataType("ULR", 32, REV, 0xffffffff, 0, 0xfffffffe, 1));
  // signed integer, -2147483647 - +2147483647, little endian
  add(new NumberDataType("SLG", 32, SIG, 0x80000000, 0x80000001, 0xffffffff, 1));
  // signed integer, -2147483647 - +2147483647, big endian
  add(new NumberDataType("SLR", 32, SIG|REV, 0x80000000, 0x80000001, 0xffffffff, 1));
  add(new NumberDataType("BI0", 7, ADJ|REQ, 0, 0, 1));  // bit 0 (up to 7 bits until bit 6)
  add(new NumberDataType("BI1", 7, ADJ|REQ, 0, 1, 1));  // bit 1 (up to 7 bits until bit 7)
  add(new NumberDataType("BI2", 6, ADJ|REQ, 0, 2, 1));  // bit 2 (up to 6 bits until bit 7)
  add(new NumberDataType("BI3", 5, ADJ|REQ, 0, 3, 1));  // bit 3 (up to 5 bits until bit 7)
  add(new NumberDataType("BI4", 4, ADJ|REQ, 0, 4, 1));  // bit 4 (up to 4 bits until bit 7)
  add(new NumberDataType("BI5", 3, ADJ|REQ, 0, 5, 1));  // bit 5 (up to 3 bits until bit 7)
  add(new NumberDataType("BI6", 2, ADJ|REQ, 0, 6, 1));  // bit 6 (up to 2 bits until bit 7)
  add(new NumberDataType("BI7", 1, ADJ|REQ, 0, 7, 1));  // bit 7
}

DataTypeList* DataTypeList::getInstance() {
  return &s_instance;
}

void DataTypeList::clear() {
  for (auto& it : m_cleanupTypes) {
    delete it;
  }
  m_cleanupTypes.clear();
  m_typesByIdLength.clear();
  m_typesById.clear();
}

result_t DataTypeList::add(const DataType* dataType) {
  if (!dataType->isAdjustableLength()) {
    ostringstream str;
    size_t bitCount = dataType->getBitCount();
    str << dataType->getId() << LENGTH_SEPARATOR << static_cast<unsigned>(bitCount >= 8?bitCount/8:bitCount);
    if (m_typesByIdLength.find(str.str()) != m_typesByIdLength.end()) {
      return RESULT_ERR_DUPLICATE_NAME;  // duplicate key
    }
    m_typesByIdLength[str.str()] = dataType;
    if (m_typesById.find(dataType->getId()) != m_typesById.end()) {
      m_cleanupTypes.push_back(dataType);
      return RESULT_OK;  // only store first one as default
    }
  } else if (m_typesById.find(dataType->getId()) != m_typesById.end()) {
    return RESULT_ERR_DUPLICATE_NAME;  // duplicate key
  }
  m_typesById[dataType->getId()] = dataType;
  m_cleanupTypes.push_back(dataType);
  return RESULT_OK;
}

const DataType* DataTypeList::get(const string& id, size_t length) const {
  if (length > 0) {
    ostringstream str;
    str << id << LENGTH_SEPARATOR << static_cast<unsigned>(length);
    auto it = m_typesByIdLength.find(str.str());
    if (it != m_typesByIdLength.end()) {
      return it->second;
    }
  }
  auto it = m_typesById.find(id);
  if (it == m_typesById.end()) {
    return nullptr;
  }
  if (length > 0 && !it->second->isAdjustableLength()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace ebusd
