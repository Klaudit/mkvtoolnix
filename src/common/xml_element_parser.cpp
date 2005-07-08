/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   $Id$

   XML element parser

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include <cctype>

#include <ebml/EbmlMaster.h>
#include <ebml/EbmlUInteger.h>
#include <ebml/EbmlString.h>
#include <ebml/EbmlUnicodeString.h>
#include <ebml/EbmlBinary.h>

#include <matroska/KaxSegment.h>

#include "base64.h"
#include "common.h"
#include "commonebml.h"
#include "error.h"
#include "mm_io.h"
#include "xml_element_parser.h"

using namespace std;
using namespace libebml;
using namespace libmatroska;

const char *
xmlp_parent_name(parser_data_t *pdata,
                 EbmlElement *e) {
  int i;

  for (i = 0; pdata->mapping[i].name != NULL; i++)
    if (pdata->mapping[i].id == e->Generic().GlobalId)
      return pdata->mapping[i].name;

  return "(none)";
}

void
xmlp_error(parser_data_t *pdata,
           const char *fmt,
           ...) {
  va_list ap;
  string new_fmt, msg_fmt;
  char *new_string;
  int len;

  msg_fmt = string("Error: ") + pdata->parser_name +
    string(" parser failed for '%s', line %d, " "column %d: ");
  len = get_arg_len(msg_fmt.c_str(), pdata->file_name,
                    XML_GetCurrentLineNumber(pdata->parser),
                    XML_GetCurrentColumnNumber(pdata->parser));
  fix_format(fmt, new_fmt);
  va_start(ap, fmt);
  len += get_varg_len(new_fmt.c_str(), ap);
  new_string = (char *)safemalloc(len + 2);
  sprintf(new_string, msg_fmt.c_str(), pdata->file_name,
          XML_GetCurrentLineNumber(pdata->parser),
          XML_GetCurrentColumnNumber(pdata->parser));
  vsprintf(&new_string[strlen(new_string)], new_fmt.c_str(), ap);
  va_end(ap);
  strcat(new_string, "\n");
  *pdata->parse_error_msg = new_string;
  safefree(new_string);
  longjmp(pdata->parse_error_jmp, 1);
}

static void
el_get_uint(parser_data_t *pdata,
            EbmlElement *el,
            uint64_t min_value = 0,
            bool is_bool = false) {
  int64 value;

  strip(*pdata->bin);
  if (!parse_int(pdata->bin->c_str(), value))
    xmlp_error(pdata, "Expected an unsigned integer but found '%s'.",
               pdata->bin->c_str());
  if (value < min_value)
    xmlp_error(pdata, "Unsigned integer (%lld) is too small. Mininum value is "
               "%lld.", value, min_value);
  if (is_bool && (value > 0))
    value = 1;

  *(static_cast<EbmlUInteger *>(el)) = value;
}

static void
el_get_string(parser_data_t *pdata,
              EbmlElement *el) {
  strip(*pdata->bin);
  *(static_cast<EbmlString *>(el)) = pdata->bin->c_str();
}

static void
el_get_utf8string(parser_data_t *pdata,
                  EbmlElement *el) {
  strip(*pdata->bin);
  *(static_cast<EbmlUnicodeString *>(el)) =
    cstrutf8_to_UTFstring(pdata->bin->c_str());
}

static void
el_get_time(parser_data_t *pdata,
            EbmlElement *el) {
  const char *errmsg = "Expected a time in the following format: HH:MM:SS.nnn"
    " (HH = hour, MM = minute, SS = second, nnn = millisecond up to "
    "nanosecond. You may use up to nine digits for 'n' which would mean "
    "nanosecond precision). Found '%s' instead. Additional error message: %s";
  int64_t usec;

  strip(*pdata->bin);
  if (!parse_timecode(pdata->bin->c_str(), &usec))
    xmlp_error(pdata, errmsg, pdata->bin->c_str(), timecode_parser_error);

  *(static_cast<EbmlUInteger *>(el)) = usec;
}

static void
el_get_binary(parser_data_t *pdata,
              EbmlElement *el,
              int min_length,
              int max_length) {
  int64_t length;
  binary *buffer;
  mm_io_c *io;

  length = 0;
  buffer = NULL;
  strip(*pdata->bin, true);
  if (pdata->bin->length() == 0)
    xmlp_error(pdata, "Found no encoded data nor '@file' to read "
               "binary data from.");

  if ((*pdata->bin)[0] == '@') {
    if (pdata->bin->length() == 1)
      xmlp_error(pdata, "No filename found after the '@'.");
    try {
      io = new mm_file_io_c(&(pdata->bin->c_str())[1]);
      io->setFilePointer(0, seek_end);
      length = io->getFilePointer();
      io->setFilePointer(0, seek_beginning);
      if (length <= 0)
        xmlp_error(pdata, "The file '%s' is empty.",
                   &(pdata->bin->c_str())[1]);
      buffer = new binary[length];
      io->read(buffer, length);
      delete io;
    } catch(...) {
      xmlp_error(pdata, "Could not open/read the file '%s'.",
                 &(pdata->bin->c_str())[1]);
    }

  } else if ((pdata->format == NULL) || !strcasecmp(pdata->format, "base64")) {
    buffer = new binary[pdata->bin->length() / 4 * 3 + 1];
    length = base64_decode(*pdata->bin, (unsigned char *)buffer);
    if (length < 0)
      xmlp_error(pdata, "Could not decode the Base64 encoded data - it seems "
                 "to be broken.");
  } else if (!strcasecmp(pdata->format, "hex")) {
    const char *p;
    bool upper;

    p = pdata->bin->c_str();
    length = 0;
    while (*p != 0) {
      if ((0 != p[1]) && ('0' == *p) && (('x' == p[1]) || ('X' == p[1]))) {
        p += 2;
        continue;
      }
      if (isdigit(*p) || ((tolower(*p) >= 'a') && (tolower(*p) <= 'f')))
        ++length;
      else if (!isblanktab(*p) && !iscr(*p) && (*p != '-') && (*p != '{') &&
               (*p != '}'))
        xmlp_error(pdata, "Invalid hexadecimal data encountered: '%c' is "
                   "neither white space nor a hexadecimal number.", *p);
      ++p;
    }
    if (((length % 2) != 0) || (length == 0))
      xmlp_error(pdata, "Too few hexadecimal digits found. The number of "
                 "digits must be > 0 and divisable by 2.");

    buffer = new binary[length / 2];
    p = pdata->bin->c_str();
    upper = true;
    length = 0;
    while (*p != 0) {
      uint8_t value;

      if ((0 != p[1]) && ('0' == *p) && (('x' == p[1]) || ('X' == p[1]))) {
        p += 2;
        continue;
      }
      if (isblanktab(*p) || iscr(*p) || (*p == '-') || (*p == '{') ||
          (*p == '}')) {
        ++p;
        continue;
      }
      value = isdigit(*p) ? *p - '0' : tolower(*p) - 'a' + 10;
      if (upper)
        buffer[length] = value << 4;
      else {
        buffer[length] |= value;
        ++length;
      }
      upper = !upper;
      ++p;
    }

  } else if (!strcasecmp(pdata->format, "ascii")) {
    length = pdata->bin->length();
    buffer = new binary[length];
    memcpy(buffer, pdata->bin->c_str(), pdata->bin->length());

  } else
    xmlp_error(pdata, "Invalid binary data format '%s' specified. Supported "
               "are 'Base64', 'ASCII' and 'hex'.");

  if ((0 < min_length) && (min_length == max_length) && (length != min_length))
    xmlp_error(pdata, "The binary data must be exactly %d bytes long.",
               min_length);
  else if ((0 < min_length) && (length < min_length))
    xmlp_error(pdata, "The binary data must be at least %d bytes long.",
               min_length);
  else if ((0 < max_length) && (length > max_length))
    xmlp_error(pdata, "The binary data must be at most %d bytes long.",
               max_length);

  (static_cast<EbmlBinary *>(el))->SetBuffer(buffer, length);
}

static void
add_data(void *user_data,
         const XML_Char *s,
         int len) {
  parser_data_t *pdata;
  int i;

  pdata = (parser_data_t *)user_data;

  if (pdata->skip_depth > 0)
    return;

  if (!pdata->data_allowed) {
    for (i = 0; i < len; i++)
      if (!isblanktab(s[i]) && !iscr(s[i]))
        xmlp_error(pdata, "Data is not allowed inside <%s>.", xmlp_pname);
    return;
  }

  if (pdata->bin == NULL)
    pdata->bin = new string;

  for (i = 0; i < len; i++)
    (*pdata->bin) += s[i];
}

static void end_element(void *user_data, const char *name);

static int
find_element_index(parser_data_t *pdata,
                   const char *name,
                   int parent_idx) {
  int elt_idx;

  elt_idx = parent_idx;
  while (pdata->mapping[elt_idx].name != NULL) {
    if (!strcmp(pdata->mapping[elt_idx].name, name))
      return elt_idx;
    elt_idx++;
  }

  return -1;
}

static void
add_new_element(parser_data_t *pdata,
                const char *name,
                int parent_idx) {
  EbmlElement *e;
  EbmlMaster *m;
  int elt_idx, i;
  bool found;

  elt_idx = find_element_index(pdata, name, parent_idx);
  if (-1 == elt_idx)
    xmlp_error(pdata, "<%s> is not a valid child element of <%s>.", name,
               pdata->mapping[parent_idx].name);

  if (pdata->depth > 0) {
    const EbmlSemanticContext &context =
      find_ebml_callbacks(KaxSegment::ClassInfos,
                          pdata->mapping[parent_idx].id).Context;
    found = false;
    for (i = 0; i < context.Size; i++)
      if (pdata->mapping[elt_idx].id ==
          context.MyTable[i].GetCallbacks.GlobalId) {
        found = true;
        break;
      }

    if (!found)
      xmlp_error(pdata, "<%s> is not a valid child element of <%s>.", name,
                 pdata->mapping[parent_idx].name);

    const EbmlSemantic &semantic =
      find_ebml_semantic(KaxSegment::ClassInfos,
                         pdata->mapping[elt_idx].id);
    if (semantic.Unique) {
      m = dynamic_cast<EbmlMaster *>(xmlp_pelt);
      assert(m != NULL);
      for (i = 0; i < m->ListSize(); i++)
        if ((*m)[i]->Generic().GlobalId == pdata->mapping[elt_idx].id)
          xmlp_error(pdata, "Only one instance of <%s> is allowed beneath "
                     "<%s>.", name, pdata->mapping[parent_idx].name);
    }
  }

  e = create_ebml_element(KaxSegment::ClassInfos,
                          pdata->mapping[elt_idx].id);
  assert(e != NULL);
  if (pdata->depth == 0) {
    m = dynamic_cast<EbmlMaster *>(e);
    assert(m != NULL);
    pdata->root_element = m;
  } else {
    m = dynamic_cast<EbmlMaster *>(xmlp_pelt);
    assert(m != NULL);
    m->PushElement(*e);
  }

  pdata->parents->push_back(e);
  pdata->parent_idxs->push_back(elt_idx);

  if (pdata->mapping[elt_idx].start_hook != NULL)
    pdata->mapping[elt_idx].start_hook(pdata);

  pdata->data_allowed = pdata->mapping[elt_idx].type != EBMLT_MASTER;

  (pdata->depth)++;
}

static void
start_element(void *user_data,
              const char *name,
              const char **atts) {
  parser_data_t *pdata;
  int elt_idx, parent_idx, i;

  pdata = (parser_data_t *)user_data;

  if (pdata->depth == 0) {
    if (pdata->done_reading)
      xmlp_error(pdata, "More than one root element found.");
    if (strcmp(name, pdata->mapping[0].name))
      xmlp_error(pdata, "The root element must be <%s>.",
                 pdata->mapping[0].name);
    parent_idx = 0;

  } else
    parent_idx = (*pdata->parent_idxs)[pdata->parent_idxs->size() - 1];

  elt_idx = find_element_index(pdata, name, parent_idx);
  if ((pdata->skip_depth > 0) ||
      ((-1 != elt_idx) && (EBMLT_SKIP == pdata->mapping[elt_idx].type))) {
    pdata->skip_depth++;
    return;
  }

  if (pdata->data_allowed)
    xmlp_error(pdata, "<%s> is not a valid child element of <%s>.", name,
               xmlp_pname);

  pdata->data_allowed = false;
  pdata->format = NULL;

  if (pdata->bin != NULL)
    die("start_element: pdata->bin != NULL");

  add_new_element(pdata, name, parent_idx);

  parent_idx = (*pdata->parent_idxs)[pdata->parent_idxs->size() - 1];
  for (i = 0; (atts[i] != NULL) && (atts[i + 1] != NULL); i += 2) {
    if (!strcasecmp(atts[i], "format"))
      pdata->format = atts[i + 1];
    else {
      pdata->bin = new string(atts[i + 1]);
      add_new_element(pdata, atts[i], parent_idx);
      end_element(pdata, atts[i]);
    }
  }
}

static void
end_element(void *user_data,
            const char *name) {
  parser_data_t *pdata;
  EbmlMaster *m;

  pdata = (parser_data_t *)user_data;

  if (pdata->skip_depth > 0) {
    pdata->skip_depth--;
    return;
  }

  if (pdata->data_allowed && (pdata->bin == NULL))
    pdata->bin = new string;

  if (pdata->depth == 1) {
    m = static_cast<EbmlMaster *>(xmlp_pelt);
    if (m->ListSize() == 0)
      xmlp_error(pdata, "At least one <EditionEntry> element is needed.");

  } else {
    int elt_idx;
    bool found;

    found = false;
    for (elt_idx = 0; pdata->mapping[elt_idx].name != NULL; elt_idx++)
      if (!strcmp(pdata->mapping[elt_idx].name, name)) {
        found = true;
        break;
      }
    assert(found);

    switch (pdata->mapping[elt_idx].type) {
      case EBMLT_MASTER:
        break;
      case EBMLT_UINT:
        el_get_uint(pdata, xmlp_pelt, pdata->mapping[elt_idx].min_value,
                    false);
        break;
      case EBMLT_BOOL:
        el_get_uint(pdata, xmlp_pelt, 0, true);
        break;
      case EBMLT_STRING:
        el_get_string(pdata, xmlp_pelt);
        break;
      case EBMLT_USTRING:
        el_get_utf8string(pdata, xmlp_pelt);
        break;
      case EBMLT_TIME:
        el_get_time(pdata, xmlp_pelt);
        break;
      case EBMLT_BINARY:
        el_get_binary(pdata, xmlp_pelt, pdata->mapping[elt_idx].min_value,
                      pdata->mapping[elt_idx].max_value);
        break;
      default:
        assert(0);
    }

    if (pdata->mapping[elt_idx].end_hook != NULL)
      pdata->mapping[elt_idx].end_hook(pdata);
  }

  if (pdata->bin != NULL) {
    delete pdata->bin;
    pdata->bin = NULL;
  }

  pdata->data_allowed = false;
  pdata->depth--;
  pdata->parents->pop_back();
  pdata->parent_idxs->pop_back();
}

EbmlMaster *
parse_xml_elements(const char *parser_name,
                   const parser_element_t *mapping,
                   mm_text_io_c *in) {
  bool done;
  parser_data_t *pdata;
  XML_Parser parser;
  XML_Error xerror;
  string buffer, error;
  EbmlMaster *root_element;

  done = false;

  parser = XML_ParserCreate(NULL);

  pdata = (parser_data_t *)safemalloc(sizeof(parser_data_t));
  memset(pdata, 0, sizeof(parser_data_t));
  pdata->parser = parser;
  pdata->file_name = in->get_file_name().c_str();
  pdata->parser_name = parser_name;
  pdata->mapping = mapping;
  pdata->parents = new vector<EbmlElement *>;
  pdata->parent_idxs = new vector<int>;
  pdata->parse_error_msg = new string;

  XML_SetUserData(parser, pdata);
  XML_SetElementHandler(parser, start_element, end_element);
  XML_SetCharacterDataHandler(parser, add_data);

  in->setFilePointer(0);

  error = "";

  try {
    if (setjmp(pdata->parse_error_jmp) == 1)
      throw error_c(*pdata->parse_error_msg);
    done = !in->getline2(buffer);
    while (!done) {
      buffer += "\n";
      if (XML_Parse(parser, buffer.c_str(), buffer.length(), done) == 0) {
        xerror = XML_GetErrorCode(parser);
        error = mxsprintf("XML parser error at line %d of '%s': %s. ",
                          XML_GetCurrentLineNumber(parser), pdata->file_name,
                          XML_ErrorString(xerror));
        if (xerror == XML_ERROR_INVALID_TOKEN)
          error += "Remember that special characters like &, <, > and \" "
            "must be escaped in the usual HTML way: &amp; for '&', "
            "&lt; for '<', &gt; for '>' and &quot; for '\"'. ";
        error += "Aborting.\n";
        throw error_c(error);
      }

      done = !in->getline2(buffer);
    }

  } catch (error_c e) {
    error = e.get_error();
  }

  root_element = pdata->root_element;
  XML_ParserFree(parser);
  delete pdata->parents;
  delete pdata->parent_idxs;
  delete pdata->parse_error_msg;
  safefree(pdata);

  if (error.length() > 0) {
    if (root_element != NULL)
      delete root_element;
    throw error_c(error);
  }

  return root_element;
}

// -------------------------------------------------------------------

static void
xml_parser_start_element_cb(void *user_data,
                            const char *name,
                            const char **atts) {
  xml_parser_c *parser = ((xml_parser_c *)user_data);

  try {
    parser->start_element_cb(name, atts);
  } catch (xml_parser_error_c &e) {
    parser->throw_error(e);
  }
}

static void
xml_parser_end_element_cb(void *user_data,
                          const char *name) {
  xml_parser_c *parser = ((xml_parser_c *)user_data);

  try {
    parser->end_element_cb(name);
  } catch (xml_parser_error_c &e) {
    parser->throw_error(e);
  }
}

static void
xml_parser_add_data_cb(void *user_data,
                       const XML_Char *s,
                       int len) {
  xml_parser_c *parser = ((xml_parser_c *)user_data);

  try {
    parser->add_data_cb(s, len);
  } catch (xml_parser_error_c &e) {
    parser->throw_error(e);
  }
}

xml_parser_c::xml_parser_c(mm_text_io_c *xml_source):
  m_xml_parser_state(XMLP_STATE_INITIAL),
  m_xml_source(xml_source),
  m_xml_parser(NULL) {
}

xml_parser_c::xml_parser_c():
  m_xml_parser_state(XMLP_STATE_INITIAL),
  m_xml_parser(NULL) {
}

void
xml_parser_c::setup_xml_parser() {
  if (NULL != m_xml_parser)
    XML_ParserFree(m_xml_parser);

  m_xml_parser = XML_ParserCreate(NULL);
  XML_SetUserData(m_xml_parser, this);
  XML_SetElementHandler(m_xml_parser, xml_parser_start_element_cb,
                        xml_parser_end_element_cb);
  XML_SetCharacterDataHandler(m_xml_parser, xml_parser_add_data_cb);
}

xml_parser_c::~xml_parser_c() {
  if (NULL != m_xml_parser)
    XML_ParserFree(m_xml_parser);
}

void
xml_parser_c::parse_xml_file() {
  m_xml_source->setFilePointer(0);

  while (parse_one_xml_line())
    ;
}

bool
xml_parser_c::parse_one_xml_line() {
  string line;

  if (NULL == m_xml_parser)
    setup_xml_parser();

  if (setjmp(m_parser_error_jmp_buf) == 1)
    throw m_saved_parser_error;

  if (!m_xml_source->getline2(line))
    return false;

  handle_xml_encoding(line);

  line += "\n";
  if (XML_Parse(m_xml_parser, line.c_str(), line.length(), false) == 0) {
    string error;
    XML_Error xerror;

    xerror = XML_GetErrorCode(m_xml_parser);
    error = XML_ErrorString(xerror);
    if (xerror == XML_ERROR_INVALID_TOKEN)
      error += "Remember that special characters like &, <, > and \" "
        "must be escaped in the usual HTML way: &amp; for '&', "
        "&lt; for '<', &gt; for '>' and &quot; for '\"'.";
    throw xml_parser_error_c(error, m_xml_parser);
  }

  return true;
}

void
xml_parser_c::throw_error(const xml_parser_error_c &error) {
  m_saved_parser_error = error;
  longjmp(m_parser_error_jmp_buf, 1);
}

void
xml_parser_c::handle_xml_encoding(string &line) {
  int pos;
  string new_line;

  if ((XMLP_STATE_AFTER_HEADER == m_xml_parser_state) ||
      (BO_NONE == m_xml_source->get_byte_order()))
    return;

  pos = 0;

  if (XMLP_STATE_INITIAL == m_xml_parser_state) {
    pos = line.find("<?xml");
    if (0 > pos)
      return;
    m_xml_parser_state = XMLP_STATE_ATTRIBUTE_NAME;
    pos += 5;
    new_line = line.substr(0, pos);
  }

  while ((line.length() > pos) &&
         (XMLP_STATE_AFTER_HEADER != m_xml_parser_state)) {
    char cur_char = line[pos];
    ++pos;

    if (XMLP_STATE_ATTRIBUTE_NAME == m_xml_parser_state) {
      if (('?' == cur_char) && (line.length() > pos) &&
          ('>' == line[pos])) {
        new_line += "?>" + line.substr(pos + 1, line.length() - pos - 1);
        m_xml_parser_state = XMLP_STATE_AFTER_HEADER;

      } else if ('"' == cur_char)
        m_xml_parser_state = XMLP_STATE_ATTRIBUTE_VALUE;

      else if ((' ' != cur_char) && ('=' != cur_char))
        m_xml_attribute_name += cur_char;

    } else {
      // XMLP_STATE_ATTRIBUTE_VALUE
      if ('"' == cur_char) {
        m_xml_parser_state = XMLP_STATE_ATTRIBUTE_NAME;
        strip(m_xml_attribute_name);
        strip(m_xml_attribute_value);
        if (m_xml_attribute_name == "encoding") {
          m_xml_attribute_value = downcase(m_xml_attribute_value);
          if ((m_xml_source->get_byte_order() == BO_NONE) &&
              ((m_xml_attribute_value == "utf-8") ||
               (m_xml_attribute_value == "utf8")))
            m_xml_source->set_byte_order(BO_UTF8);

          else if (starts_with_case(m_xml_attribute_value, "utf"))
            m_xml_attribute_value = "UTF-8";
        }

        new_line += " " + m_xml_attribute_name + "=\"" +
          m_xml_attribute_value + "\"";
        m_xml_attribute_name = "";
        m_xml_attribute_value = "";

      } else
        m_xml_attribute_value += cur_char;
    }
  }

  line = new_line;
}

