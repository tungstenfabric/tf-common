/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <cassert>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <boost/static_assert.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <base/logging.h>
#include <base/util.h>
#include <base/string_util.h>

#include "TJSONProtocol.h"

using std::string;

#ifdef TJSONPROTOCOL_DEBUG_PRETTY_PRINT
static const string endl = "\n";
#else
static const string endl = "";
#endif // !TJSONPROTOCOL_DEBUG_PRETTY_PRINT

namespace contrail { namespace sandesh { namespace protocol {

// Static data

static const std::string kJSONTagO("{");
static const std::string kJSONTagC("}");
static const std::string kJSONType("type");
static const std::string kJSONIdentifier("identifier");
static const std::string kJSONName("name");
static const std::string kJSONKey("key");
static const std::string kJSONValue("value");
static const std::string kJSONSize("size");
static const std::string kJSONBoolTrue("true");
static const std::string kJSONBoolFalse("false");
static const std::string kJSONCDATAO("<![CDATA[");
static const std::string kJSONCDATAC("]]>");

static const std::string kTypeNameBool("bool");
static const std::string kTypeNameByte("byte");
static const std::string kTypeNameI16("i16");
static const std::string kTypeNameI32("i32");
static const std::string kTypeNameI64("i64");
static const std::string kTypeNameU16("u16");
static const std::string kTypeNameU32("u32");
static const std::string kTypeNameU64("u64");
static const std::string kTypeNameIPV4("ipv4");
static const std::string kTypeNameIPADDR("ipaddr");
static const std::string kTypeNameDouble("double");
static const std::string kTypeNameStruct("struct");
static const std::string kTypeNameString("string");
static const std::string kTypeNameXML("xml");
static const std::string kTypeNameUUID("uuid_t");
static const std::string kTypeNameMap("map");
static const std::string kTypeNameList("list");
static const std::string kTypeNameSet("set");
static const std::string kTypeNameSandesh("sandesh");
static const std::string kTypeNameUnknown("unknown");

static const std::string kAttrTypeSandesh("type=\"sandesh\"");
static const std::string kJSONListTagO("<list ");
static const std::string kJSONListTagC("</list>" + endl);
static const std::string kJSONSetTagO("<set ");
static const std::string kJSONSetTagC("</set>" + endl);
static const std::string kJSONMapTagO("<map ");
static const std::string kJSONMapTagC("</map>" + endl);
static const std::string kType("\"TYPE\":");
static const std::string kTYPEStruct("TYPE: STRUCT");
static const std::string kFieldId("\"ID\":");
static const std::string kVal("\"VAL\":");

const std::string& TJSONProtocol::fieldTypeName(TType type) {
  switch (type) {
    case T_BOOL   : return kTypeNameBool   ;
    case T_BYTE   : return kTypeNameByte   ;
    case T_I16    : return kTypeNameI16    ;
    case T_I32    : return kTypeNameI32    ;
    case T_I64    : return kTypeNameI64    ;
    case T_U16    : return kTypeNameU16    ;
    case T_U32    : return kTypeNameU32    ;
    case T_U64    : return kTypeNameU64    ;
    case T_IPV4   : return kTypeNameIPV4   ;
    case T_IPADDR : return kTypeNameIPADDR ;
    case T_DOUBLE : return kTypeNameDouble ;
    case T_STRING : return kTypeNameString ;
    case T_STRUCT : return kTypeNameStruct ;
    case T_MAP    : return kTypeNameMap    ;
    case T_SET    : return kTypeNameSet    ;
    case T_LIST   : return kTypeNameList   ;
    case T_SANDESH: return kTypeNameSandesh;
    case T_XML    : return kTypeNameXML    ;
    case T_UUID   : return kTypeNameUUID   ;
    default: return kTypeNameUnknown;
  }
}

TType TJSONProtocol::getTypeIDForTypeName(const std::string &name) {
  TType result = T_STOP; // Sentinel value
  if (name.length() > 1) {
    switch (name[0]) {
    case 'b':
        switch (name[1]) {
        case 'o':
            result = T_BOOL;
            break;
        case 'y':
            result = T_BYTE;
            break;
        }
        break;
    case 'd':
      result = T_DOUBLE;
      break;
    case 'i':
      switch (name[1]) {
      case '1':
        result = T_I16;
        break;
      case '3':
        result = T_I32;
        break;
      case '6':
        result = T_I64;
        break;
      case 'p':
        switch (name[2]) {
        case 'a':
          result = T_IPADDR;
          break;
        case 'v':
          result = T_IPV4;
          break;
        }
        break;
      }
      break;
    case 'l':
      result = T_LIST;
      break;
    case 'm':
      result = T_MAP;
      break;
    case 's':
        switch (name[1]) {
        case 'a':
            result = T_SANDESH;
            break;
        case 'e':
            result = T_SET;
            break;
        case 't':
            switch (name[3]) {
            case 'i':
                result = T_STRING;
                break;
            case 'u':
                result = T_STRUCT;
                break;
            }
            break;
        }
        break;
    case 'u':
      switch(name[1]) {
        case '1':
          result = T_U16;
          break;
        case '3':
          result = T_U32;
          break;
        case '6':
          result = T_U64;
          break;
        case 'u':
          result = T_UUID;
          break;
      }
      break;
    case 'x':
      result = T_XML;
      break;
    }
  }
  if (result == T_STOP) {
    LOG(ERROR, __func__ << "Unrecognized type: " << name);
  }
  return result;
}

static inline void formJSONAttr(std::string &dest, const std::string& name, 
                               const std::string & value) {
  dest += name;
  dest += "=\"";
  dest += value; 
  dest += "\"";
}

void TJSONProtocol::indentUp() {
#if TJSONPROTOCOL_DEBUG_PRETTY_PRINT
  indent_str_ += string(indent_inc, ' ');
#endif // !TJSONPROTOCOL_DEBUG_PRETTY_PRINT
}

void TJSONProtocol::indentDown() {
#if TJSONPROTOCOL_DEBUG_PRETTY_PRINT
  if (indent_str_.length() < (string::size_type)indent_inc) {
    LOG(ERROR, __func__ << "Indent string length " <<
        indent_str_.length() << " less than indent length " <<
        (string::size_type)indent_inc);
    return;
  }
  indent_str_.erase(indent_str_.length() - indent_inc);
#endif // !TJSONPROTOOL_DEBUG_PRETTY_PRINT
}

// Returns the number of bytes written on success, -1 otherwise
int32_t TJSONProtocol::writePlain(const string& str) {
  int ret = trans_->write((uint8_t*)str.data(), str.length());
  if (ret) {
      return -1;
  }
  return str.length();
}

// Returns the number of bytes written on success, -1 otherwise
int32_t TJSONProtocol::writeIndented(const string& str) {
  int ret;
#if TJSONPROTOCOL_DEBUG_PRETTY_PRINT
  ret = trans_->write((uint8_t*)indent_str_.data(), indent_str_.length());
  if (ret) {
      return -1;
  }
#endif // !TJSONPROTOCOL_DEBUG_PRETTY_PRINT
  ret = trans_->write((uint8_t*)str.data(), str.length());
  if (ret) {
     return -1;
  }
  return indent_str_.length() + str.length();
}

int32_t TJSONProtocol::writeMessageBegin(const std::string& name,
                                        const TMessageType messageType,
                                        const int32_t seqid) {
  return 0;
}

int32_t TJSONProtocol::writeMessageEnd() {
  return 0;
}


/*
* As a part of structBegin call, we open 2 {{
* Inside the outer bracket, we will have
* meta data about the structure itself.
* Inside the inner bracket, we will have the
* actual fields of the structure
*/
int32_t TJSONProtocol::writeStructBegin(const char* name) {
  int32_t size = 0, ret;
  string sname(name);
  string json;
  json.reserve(512);

  // Handle list of struct
  if (is_first_element_context_.size() > 0 &&
      !is_first_element_context_.back() &&
      current_sandesh_context_.back() == "LIST") {
      json += ",";
  } else if (!is_first_element_context_.empty()) {
      is_first_element_context_.back() = false;
  }

  json += "{";
  if(current_sandesh_context_.back() == "SANDESH") {
     json += "\"STAT_TYPE\":";
     json += "\""+sname+"\"";
     json += ",";
  }
  current_sandesh_context_.push_back("STRUCT");
  is_first_element_context_.push_back(true);
  is_primitive_element_list_.push_back(false);
  json += "\"VAL\":";
  json += kJSONTagO;
  indentUp();
  // Write to transport
  if ((ret = writeIndented(json)) < 0) {
    LOG(ERROR, __func__ << ": " << sname << " FAILED");
    return ret;
  }
  // push a value to is_struct_begin
  is_struct_begin_list_.push_back(true);
  size += ret;
  return size;
}

/*
 * Close the brackets that were opened as part of
 * struct begin. Some variables are maintained to
 * identify if we are in struct context or not.
 */
int32_t TJSONProtocol::writeStructEnd() {
  int32_t size = 0, ret;
  indentDown();
  string json;
  json.reserve(128);
  json += kJSONTagC;
  indentDown();
  json += endl;
  json += kJSONTagC;
  json += endl;
  indentDown();
  // Write to transport
  if ((ret = writeIndented(json)) < 0) {
    LOG(ERROR, __func__ << ": " << json << " FAILED");
    return ret;
  }
  size += ret;

  current_sandesh_context_.pop_back();
  is_first_element_context_.pop_back();
  if(is_first_element_context_.size() > 0) {
      is_first_element_context_.back() = false;
  }
  is_primitive_element_list_.pop_back();
  return size;
}

/*
 * Open a couple of brackets when this call is made
 * The first bracket to store metadata about the
 * sandesh. The second bracket to store the actual
 * fields of the sandesh.
 */
int32_t TJSONProtocol::writeSandeshBegin(const char* name) {
  int32_t size = 0, ret;
  string sname(name);
  string json;
  current_sandesh_context_.push_back("SANDESH");
  is_first_element_context_.push_back(true);
  json.reserve(512);
  json += kJSONTagO;
  json += endl;
  indentUp();
  json += "\"";
  json += name;
  json += "\"";
  json += ":";
  json += kJSONTagO;
  json += endl;
  indentUp();
  // Write to transport
  ret = writeIndented(json);
  if (ret < 0) {
    LOG(ERROR, __func__ << ": " << sname << " FAILED");
    return ret;
  }
  size += ret;
  is_primitive_element_list_.push_back(false);
  indentUp();
  return size;
}

int32_t TJSONProtocol::writeSandeshEnd() {

  int32_t size = 0, ret;
  indentDown();
  string json;
  json.reserve(128);
  //indentDown();
  json += kJSONTagC;
  json += endl;
  indentDown();
  json += ",";
  json += "\"TIMESTAMP\":";
  std::stringstream ss;
  std::string t_s;
  ss << UTCTimestampUsec();
  ss >> t_s;
  json += t_s;
  json += kJSONTagC;
  json += endl;
  // Write to transport
  if ((ret = writeIndented(json)) < 0) {
    LOG(ERROR, __func__ << ": " << json << " FAILED");
    return ret;
  }
  size += ret;
  current_sandesh_context_.pop_back();
  is_first_element_context_.pop_back();
  is_primitive_element_list_.pop_back();
  return size;
}

int32_t TJSONProtocol::writeContainerElementBegin() {
  int32_t size = 0, ret;
  indentDown();
  string json;
  json.reserve(128);

  if (!is_first_element_context_.back()) {
      // could be in map or list context, print , only in case of map val
      if (current_sandesh_context_.back() == "MAP") {
          if (!in_map_val_context_.back()) {
              // print only before keys
              json += ",";
          }
      } else {
          json += ",";
      }
  } else {
     is_first_element_context_.back() = false;
  }

  if (current_sandesh_context_.back() == "LIST" &&
      is_primitive_element_list_.back()) {
      if(is_list_elem_string_) {
          json += "\"";
      }
  }

  if (current_sandesh_context_.back() == "MAP") {
     if (is_primitive_element_list_.back()) {
         json+= "\"";
     }
  }
  return writeIndented(json);
  //return writeIndented(kJSONElementTagO);
}

/*
 * This fn will be called for primitive
 * types.
 */
int32_t TJSONProtocol::writeContainerElementEnd() {
  int32_t size = 0, ret;
  indentDown();
  string json;
  json.reserve(128);

  if (current_sandesh_context_.back() == "MAP") {
      if (is_primitive_element_list_.back()) {
          json += "\"";
      }
      // print : if its map key else dont
      if (in_map_val_context_.back()) {
          in_map_val_context_.back() = false;
      } else {
          // In map key context
          json += ":";
          if (is_map_val_primitive_.back()) {
              in_map_val_context_.back() = true;
          }
      }
  }

  if (current_sandesh_context_.back() == "LIST" &&
      is_primitive_element_list_.back()) {
      if (is_list_elem_string_) {
          json += "\"";
      }
  }

  return writeIndented(json);
}


/*
 * This function is called for every field
 * of type struct. JSON uses "," seperated
 * entries to hold list of values. We
 * maintain state variables to add comma or
 * not after each entry.
 * First we write meta data about the field,
 * then we store the actual value under the
 * tag "VAL". The new stats collections are
 * decided based on the annotations of the individual
 * fields.
 */
int32_t TJSONProtocol::writeFieldBegin(const char *name,
                                      const TType fieldType,
                                      const int16_t fieldId,
                                      const std::map<std::string, std::string> *const amap) {
  int32_t size = 0, ret;
  string sname(name);
  string json;
  json.reserve(512);

  if (!is_first_element_context_.back() &&
      current_sandesh_context_.back() != "MAP") {
      json += ",";
  } else {
      is_first_element_context_.back() = false;
  }

  json += "\"";
  json += sname;
  json += "\"";
  json += ":";
  json += kJSONTagO; // : {
  json += endl;
  indentUp();
  json += kType; // "TYPE":
  json += "\"";
  json += fieldTypeName(fieldType);
  json += "\"";
  json += ",";
  json += endl;

  if(amap != NULL) {
      json += "\"ANNOTATION\":";
      json += "{";
      std::map<std::string, std::string>::const_iterator it;
      for(it = amap->begin(); it != amap->end(); it++) {
          json += "\""+it->first+"\"";
          json += ":";
          json += "\""+it->second+"\"";
          json += ",";
      }
      json.erase(json.size()-1);
      json += "},";
  }

  json += "\"VAL\":";
  //If field type is string quote it
  if(fieldType == T_STRING || fieldType == T_IPADDR || fieldType == T_UUID) {
      is_string_begin_ = true;
      json += "\"";
  }
  json += endl;
  // Write to transport
  if ((ret = writeIndented(json)) < 0) {
    LOG(ERROR, __func__ << ": " << json << " FAILED");
    return ret;
  }
  size += ret;
  return size;
}

int32_t TJSONProtocol::writeFieldEnd() {
  int32_t size = 0, ret;
  string json;
  indentDown();
  if(is_string_begin_) {
      is_string_begin_ = false;
      json += "\"";
  }
  json += "}";
  if ((ret = writeIndented(json)) < 0) {
    LOG(ERROR, __func__ << ": " << json << " FAILED");
    return ret;
  }
  size += ret;
  return size;
}

int32_t TJSONProtocol::writeFieldStop() {
  return 0;
}

int32_t TJSONProtocol::writeMapBegin(const TType keyType,
                                    const TType valType,
                                    const uint32_t size) {

  int32_t bsize = 0, ret;
  string json;
  json.reserve(256);
  current_sandesh_context_.push_back("MAP");
  is_first_element_context_.push_back(true);
  json += "{";
  json += endl;
  json += "\"VALUE\":";
  json += "\"";
  json += fieldTypeName(valType);
  json += "\"";
  json += ",";
  json += "\"VAL\":";
  json += "{";

  indentUp();
  // Write to transport
  ret = writeIndented(json);
  if (ret < 0) {
    LOG(ERROR, __func__ << ": Key: " << fieldTypeName(keyType) <<
        " Value: " << fieldTypeName(valType) << " FAILED");
    return ret;
  }

  if (keyType == T_MAP || keyType == T_STRUCT || keyType == T_LIST) {
      is_primitive_element_list_.push_back(false);
  } else {
      is_primitive_element_list_.push_back(true);
  }

  if(valType == T_MAP || valType == T_STRUCT || valType == T_LIST) {
     in_non_primitive_map_context_ = true;
     is_map_val_primitive_.push_back(false);
     in_map_val_context_.push_back(false);
  } else {
     is_map_primitive_ = true;
     is_map_val_primitive_.push_back(true);
     in_map_val_context_.push_back(false);
  }

  bsize += ret;
  indentUp();
  return bsize;

}

int32_t TJSONProtocol::writeMapEnd() {
  int32_t size = 0, ret;
  string json;
  json.reserve(256);
  indentDown();
  json += "}";
  json += endl;

  indentDown();
  json += "}";
  json += endl;


  if ((ret = writeIndented(json)) < 0) {
    LOG(ERROR, __func__ << " FAILED");
    return ret;
  }
  current_sandesh_context_.pop_back();
  is_first_element_context_.pop_back();
  if (is_first_element_context_.size() > 0) {
      is_first_element_context_.back() = false;
  }
  is_primitive_element_list_.pop_back();
  in_map_val_context_.pop_back();
  is_map_val_primitive_.pop_back();
  size += ret;
  return size;
}

int32_t TJSONProtocol::writeListBegin(const TType elemType,
                                     const uint32_t size) {
  int32_t bsize = 0, ret;
  string json;
  json.reserve(256);

  if (is_first_element_context_.size() > 0 &&
      !is_first_element_context_.back() &&
      current_sandesh_context_.back() == "LIST") {
      json += ",";
  } else if (is_first_element_context_.size() > 0) {
      is_first_element_context_.back() = false;
  }

  current_sandesh_context_.push_back("LIST");
  is_first_element_context_.push_back(true);
  json += "{";
  json += "\"VAL\":";
  json += "[";
  json += endl;
  // Write to transport
  ret = writeIndented(json);
  if (ret < 0) {
    LOG(ERROR, __func__ << ": " << fieldTypeName(elemType) <<
        " FAILED");
    return ret;
  }
  is_list_begin_list_.push_back(true);
  if(elemType != T_STRUCT || elemType != T_MAP ) {
      //primitive type
      if(elemType == T_STRING || elemType == T_IPADDR || elemType == T_UUID) {
          is_list_elem_string_ = true;
      }
      is_primitive_element_list_.push_back(true);
      //in_list_context_ = true;
  } else {
      is_primitive_element_list_.push_back(false);
  }
  bsize += ret;
  indentUp();
  return bsize;
}

int32_t TJSONProtocol::writeListEnd() {
  int32_t size = 0, ret;
  string json;
  json.reserve(32);
  indentDown();
  json += "]";
  indentDown();
  json += "}";
  ret = writeIndented(json);
  
  if (ret < 0) {
    LOG(ERROR, __func__ << " FAILED");
    return ret;
  }
  size += ret;
  current_sandesh_context_.pop_back();
  is_first_element_context_.pop_back();
  if (is_first_element_context_.size() > 0) {
      is_first_element_context_.back() = false;
  }
  is_primitive_element_list_.pop_back();
  return size;
}

int32_t TJSONProtocol::writeSetBegin(const TType elemType,
                                    const uint32_t size) {
  int32_t bsize = 0, ret;
  string json;
  json.reserve(256);

  if (is_first_element_context_.size() > 0 &&
      !is_first_element_context_.back() &&
      current_sandesh_context_.back() == "SET") {
      json += ",";
  } else if (is_first_element_context_.size() > 0) {
      is_first_element_context_.back() = false;
  }

  current_sandesh_context_.push_back("SET");
  is_first_element_context_.push_back(true);
  json += "{";
  json += "\"VAL\":";
  json += "[";
  json += endl;
  // Write to transport
  ret = writeIndented(json);
  if (ret < 0) {
    LOG(ERROR, __func__ << ": " << fieldTypeName(elemType) <<
        " FAILED");
    return ret;
  }
  is_list_begin_list_.push_back(true);
  if(elemType != T_STRUCT || elemType != T_MAP ) {
      //primitive type
      if(elemType == T_STRING || elemType == T_IPADDR || elemType == T_UUID) {
          is_list_elem_string_ = true;
      }
      is_primitive_element_list_.push_back(true);
      //in_list_context_ = true;
  } else {
      is_primitive_element_list_.push_back(false);
  }
  bsize += ret;
  indentUp();
  return bsize;
}

int32_t TJSONProtocol::writeSetEnd() {
  int32_t size = 0, ret;
  string json;
  json.reserve(32);
  indentDown();
  json += "]";
  indentDown();
  json += "}";
  ret = writeIndented(json);
  
  if (ret < 0) {
    LOG(ERROR, __func__ << " FAILED");
    return ret;
  }
  size += ret;
  current_sandesh_context_.pop_back();
  is_first_element_context_.pop_back();
  if (is_first_element_context_.size() > 0) {
      is_first_element_context_.back() = false;
  }
  is_primitive_element_list_.pop_back();
  return size;
}

int32_t TJSONProtocol::writeBool(const bool value) {
  int32_t size = 0, ret;
  return writePlain(value ? kJSONBoolTrue : kJSONBoolFalse);
}

int32_t TJSONProtocol::writeByte(const int8_t byte) {
  return writePlain(integerToString(byte));
}

int32_t TJSONProtocol::writeI16(const int16_t i16) {
  return writePlain(integerToString(i16));
}

int32_t TJSONProtocol::writeI32(const int32_t i32) {
  int32_t size = 0, ret;
  string json;
  json.reserve(512);
  json += integerToString(i32);
  return writePlain(json);
}

int32_t TJSONProtocol::writeI64(const int64_t i64) {
  return writePlain(integerToString(i64));
}

int32_t TJSONProtocol::writeU16(const uint16_t u16) {
  return writePlain(integerToString(u16));
}

int32_t TJSONProtocol::writeU32(const uint32_t u32) {
  return writePlain(integerToString(u32));
}

int32_t TJSONProtocol::writeU64(const uint64_t u64) {
  return writePlain(integerToString(u64));
}

int32_t TJSONProtocol::writeIPV4(const uint32_t ip4) {
  return writePlain(integerToString(ip4));
}

int32_t TJSONProtocol::writeIPADDR(const boost::asio::ip::address& ipaddress) {
  return writePlain(ipaddress.to_string());
}

int32_t TJSONProtocol::writeDouble(const double dub) {
  return writePlain(integerToString(dub));
}

int32_t TJSONProtocol::writeString(const string& str) {
  int32_t size = 0, ret;
  string json;
  json.reserve(512);
  json += str;
  // Escape JSON control characters in the string before writing
  return writePlain(escapeJSONControlChars(json));
}

int32_t TJSONProtocol::writeBinary(const string& str) {
  // XXX Hex?
  return writeString(str);
}

int32_t TJSONProtocol::writeUUID(const boost::uuids::uuid& uuid) {
  const std::string str = boost::uuids::to_string(uuid);
  return writeString(str);
}

}}} // contrail::sandesh::protocol
