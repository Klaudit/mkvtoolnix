/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   OGG media stream reader

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/os.h"
#include "common/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#if defined(SYS_WINDOWS)
#include <windows.h>
#include <time.h>
#endif

extern "C" {                    // for BITMAPINFOHEADER
#include "avilib.h"
}

#include "common/aac_common.h"
#include "common/chapters.h"
#include "common/common.h"
#include "common/ebml.h"
#include "common/endian.h"
#include "common/hacks.h"
#include "common/iso639.h"
#include "common/matroska.h"
#include "common/mpeg4_p2.h"
#include "common/locale.h"
#include "common/ogmstreams.h"
#include "input/r_ogm.h"
#include "input/r_ogm_flac.h"
#include "merge/output_control.h"
#include "merge/pr_generic.h"
#include "output/p_aac.h"
#include "output/p_ac3.h"
#include "output/p_avc.h"
#include "output/p_kate.h"
#include "output/p_mp3.h"
#include "output/p_mpeg4_p2.h"
#include "output/p_pcm.h"
#include "output/p_textsubs.h"
#include "output/p_theora.h"
#include "output/p_video.h"
#include "output/p_vorbis.h"

#define BUFFER_SIZE 4096

struct ogm_frame_t {
  memory_c *mem;
  int64_t duration;
  unsigned char flags;
};

class ogm_a_aac_demuxer_c: public ogm_demuxer_c {
public:

public:
  ogm_a_aac_demuxer_c(ogm_reader_c *p_reader);

  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_AUDIO;
  };
  virtual string get_codec() {
    return "AAC";
  };
};

class ogm_a_ac3_demuxer_c: public ogm_demuxer_c {
public:

public:
  ogm_a_ac3_demuxer_c(ogm_reader_c *p_reader);

  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_AUDIO;
  };
  virtual string get_codec() {
    return "AC3";
  };
};

class ogm_a_mp3_demuxer_c: public ogm_demuxer_c {
public:

public:
  ogm_a_mp3_demuxer_c(ogm_reader_c *p_reader);

  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_AUDIO;
  };
  virtual string get_codec() {
    return "MP2/MP3";
  };
};

class ogm_a_pcm_demuxer_c: public ogm_demuxer_c {
public:

public:
  ogm_a_pcm_demuxer_c(ogm_reader_c *p_reader);

  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_AUDIO;
  };
  virtual string get_codec() {
    return "PCM";
  };
};

class ogm_a_vorbis_demuxer_c: public ogm_demuxer_c {
public:

public:
  ogm_a_vorbis_demuxer_c(ogm_reader_c *p_reader);

  virtual void process_page(int64_t granulepos);

  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_AUDIO;
  };
  virtual string get_codec() {
    return "Vorbis";
  };
};

class ogm_s_text_demuxer_c: public ogm_demuxer_c {
public:

public:
  ogm_s_text_demuxer_c(ogm_reader_c *p_reader);

  virtual void process_page(int64_t granulepos);

  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_SUBTITLES;
  };
  virtual string get_codec() {
    return "Text";
  };
};

class ogm_v_avc_demuxer_c: public ogm_demuxer_c {
public:

public:
  ogm_v_avc_demuxer_c(ogm_reader_c *p_reader);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_VIDEO;
  };

  virtual string get_codec() {
    return "h.264/AVC";
  };

  virtual void initialize();
  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);

private:
  virtual memory_cptr extract_avcc();
};

class ogm_v_mscomp_demuxer_c: public ogm_demuxer_c {
public:
  int64_t frames_since_granulepos_change;

public:
  ogm_v_mscomp_demuxer_c(ogm_reader_c *p_reader);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_VIDEO;
  };

  virtual string get_codec();

  virtual void initialize();
  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);
  virtual void process_page(int64_t granulepos);
};

class ogm_v_theora_demuxer_c: public ogm_demuxer_c {
public:
  theora_identification_header_t theora;

public:
  ogm_v_theora_demuxer_c(ogm_reader_c *p_reader);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_VIDEO;
  };

  virtual string get_codec() {
    return "Theora";
  };

  virtual void initialize();
  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);
  virtual void process_page(int64_t granulepos);
  virtual bool is_header_packet(ogg_packet &op);
};

class ogm_s_kate_demuxer_c: public ogm_demuxer_c {
public:
  kate_identification_header_t kate;

public:
  ogm_s_kate_demuxer_c(ogm_reader_c *p_reader);

  virtual const char *get_type() {
    return ID_RESULT_TRACK_SUBTITLES;
  };

  virtual string get_codec() {
    return "Kate";
  };

  virtual void initialize();
  virtual generic_packetizer_c *create_packetizer(track_info_c &ti);
  virtual void process_page(int64_t granulepos);
  virtual bool is_header_packet(ogg_packet &op);
};

// ---------------------------------------------------------------------------------

static counted_ptr<vector<string> >
extract_vorbis_comments(const memory_cptr &mem) {
  counted_ptr<vector<string> > comments(new vector<string>);
  mm_mem_io_c in(mem->get(), mem->get_size());
  uint32_t i, n, len;

  try {
    in.skip(7);                 // 0x03 "vorbis"
    n = in.read_uint32_le();    // vendor_length
    in.skip(n);                 // vendor_string
    n = in.read_uint32_le();    // user_comment_list_length

    comments->reserve(n);

    for (i = 0; i < n; i++) {
      len = in.read_uint32_le();
      memory_cptr buffer(new memory_c((unsigned char *)safemalloc(len + 1), len + 1));

      if (in.read(buffer->get(), len) != len)
        throw false;

      comments->push_back(string((char *)buffer->get(), len));
    }
  } catch(...) {
  }

  return comments;
}

/*
   Probes a file by simply comparing the first four bytes to 'OggS'.
*/
int
ogm_reader_c::probe_file(mm_io_c *io,
                         int64_t size) {
  unsigned char data[4];

  if (4 > size)
    return 0;
  try {
    io->setFilePointer(0, seek_beginning);
    if (io->read(data, 4) != 4)
      return 0;
    io->setFilePointer(0, seek_beginning);
  } catch (...) {
    return 0;
  }
  if (strncmp((char *)data, "OggS", 4))
    return 0;
  return 1;
}

/*
   Opens the file for processing, initializes an ogg_sync_state used for
   reading from an OGG stream.
*/
ogm_reader_c::ogm_reader_c(track_info_c &_ti)
  throw (error_c):
  generic_reader_c(_ti) {

  try {
    io        = new mm_file_io_c(ti.fname);
    file_size = io->get_size();
  } catch (...) {
    throw error_c(Y("ogm_reader: Could not open the source file."));
  }
  if (!ogm_reader_c::probe_file(io, file_size))
    throw error_c(Y("ogm_reader: Source is not a valid OGG media file."));

  ogg_sync_init(&oy);

  if (verbose)
    mxinfo_fn(ti.fname, Y("Using the OGG/OGM demultiplexer.\n"));

  if (read_headers() <= 0)
    throw error_c(Y("ogm_reader: Could not read all header packets."));
  handle_stream_comments();
}

ogm_reader_c::~ogm_reader_c() {
  ogg_sync_clear(&oy);
  delete io;
}

ogm_demuxer_cptr
ogm_reader_c::find_demuxer(int serialno) {
  int i;

  for (i = 0; i < sdemuxers.size(); i++)
    if (sdemuxers[i]->serialno == serialno) {
      if (sdemuxers[i]->in_use)
        return sdemuxers[i];
      return ogm_demuxer_cptr(NULL);
    }

  return ogm_demuxer_cptr(NULL);
}

/*
   Reads an OGG page from the stream. Returns 0 if there are no more pages
   left, EMOREDATA otherwise.
*/
int
ogm_reader_c::read_page(ogg_page *og) {
  int np, done, nread;
  unsigned char *buf;

  done = 0;
  while (!done) {
    np = ogg_sync_pageseek(&oy, og);

    // np == 0 means that there is not enough data for a complete page.
    if (0 >= np) {
      // np < 0 is the error case. Should not happen with local OGG files.
      if (0 > np)
        mxwarn_fn(ti.fname, Y("Could not find the next Ogg page. This indicates a damaged Ogg/Ogm file. Will try to continue.\n"));

      buf = (unsigned char *)ogg_sync_buffer(&oy, BUFFER_SIZE);
      if (!buf)
        mxerror_fn(ti.fname, Y("ogg_sync_buffer failed\n"));

      if (0 >= (nread = io->read(buf, BUFFER_SIZE)))
        return 0;

      ogg_sync_wrote(&oy, nread);
    } else
      // Alright, we have a page.
      done = 1;
  }

  // Here EMOREDATA actually indicates success - a page has been read.
  return FILE_STATUS_MOREDATA;
}

void
ogm_reader_c::create_packetizer(int64_t tid) {
  ogm_demuxer_cptr dmx;
  generic_packetizer_c *ptzr;

  if ((0 > tid) || (sdemuxers.size() <= tid))
    return;
  dmx = sdemuxers[tid];

  if (!dmx->in_use)
    return;

  ti.private_data = NULL;
  ti.private_size = 0;
  ti.id           = tid;
  ti.language     = dmx->language;
  ti.track_name   = dmx->title;

  ptzr            = dmx->create_packetizer(ti);

  if (NULL != ptzr)
    dmx->ptzr     = add_packetizer(ptzr);

  ti.language.clear();
  ti.track_name.clear();
}

void
ogm_reader_c::create_packetizers() {
  int i;

  for (i = 0; i < sdemuxers.size(); i++)
    create_packetizer(i);
}

/*
   Checks every demuxer if it has a page available.
*/
int
ogm_reader_c::packet_available() {
  int i;

  if (sdemuxers.size() == 0)
    return 0;

  for (i = 0; i < sdemuxers.size(); i++)
    if ((-1 != sdemuxers[i]->ptzr) && !PTZR(sdemuxers[i]->ptzr)->packet_available())
      return 0;

  return 1;
}

void
ogm_reader_c::handle_new_stream_and_packets(ogg_page *og) {
  ogm_demuxer_cptr dmx;

  handle_new_stream(og);
  dmx = find_demuxer(ogg_page_serialno(og));
  if (dmx.get())
    process_header_packets(dmx);
}

/*
   The page is the beginning of a new stream. Check the contents for known
   stream headers. If it is a known stream and the user has requested that
   it should be extracted then allocate a new packetizer based on the
   stream type and store the needed data in a new ogm_demuxer_c.
*/
void
ogm_reader_c::handle_new_stream(ogg_page *og) {
  ogg_stream_state os;
  ogg_packet op;

  if (ogg_stream_init(&os, ogg_page_serialno(og))) {
    mxwarn_fn(ti.fname, boost::format(Y("ogg_stream_init for stream number %1% failed. Will try to continue and ignore this stream.\n")) % sdemuxers.size());
    return;
  }

  // Read the first page and get its first packet.
  ogg_stream_pagein(&os, og);
  ogg_stream_packetout(&os, &op);

  ogm_demuxer_c *dmx = NULL;

  /*
   * Check the contents for known stream headers. This one is the
   * standard Vorbis header.
   */
  if ((7 <= op.bytes) && !strncmp((char *)&op.packet[1], "vorbis", 6))
    dmx = new ogm_a_vorbis_demuxer_c(this);

  else if ((7 <= op.bytes) && !strncmp((char *)&op.packet[1], "theora", 6))
    dmx = new ogm_v_theora_demuxer_c(this);

  else if ((8 <= op.bytes) && !memcmp(&op.packet[1], "kate\0\0\0", 7))
    dmx = new ogm_s_kate_demuxer_c(this);

  // FLAC
  else if ((4 <= op.bytes) && !strncmp((char *)op.packet, "fLaC", 4)) {
#if !defined(HAVE_FLAC_FORMAT_H)
    if (demuxing_requested('a', sdemuxers.size()))
      mxerror_fn(ti.fname, Y("mkvmerge has not been compiled with FLAC support but handling of this stream has been requested.\n"));

    else {
      dmx         = new ogm_demuxer_c(this);
      dmx->stype  = OGM_STREAM_TYPE_A_FLAC;
      dmx->in_use = true;
    }

#else
    dmx = new ogm_a_flac_demuxer_c(this);
#endif

  } else if (((*op.packet & PACKET_TYPE_BITS ) == PACKET_TYPE_HEADER) && (op.bytes >= ((int)sizeof(stream_header) + 1))) {
    // The new stream headers introduced by OggDS (see ogmstreams.h).

    stream_header *sth = (stream_header *)(op.packet + 1);
    char buf[5];
    buf[4] = 0;

    if (!strncmp(sth->streamtype, "video", 5)) {
      memcpy(buf, (char *)sth->subtype, 4);

      if (mpeg4::p10::is_avc_fourcc(buf) && !hack_engaged(ENGAGE_ALLOW_AVC_IN_VFW_MODE))
        dmx = new ogm_v_avc_demuxer_c(this);
      else
        dmx = new ogm_v_mscomp_demuxer_c(this);

    } else if (!strncmp(sth->streamtype, "audio", 5)) {
      memcpy(buf, (char *)sth->subtype, 4);

      uint32_t codec_id = strtol(buf, (char **)NULL, 16);

      if (0x0001 == codec_id)
        dmx = new ogm_a_pcm_demuxer_c(this);

      else if ((0x0050 == codec_id) || (0x0055 == codec_id))
        dmx = new ogm_a_mp3_demuxer_c(this);

      else if (0x2000 == codec_id)
        dmx = new ogm_a_ac3_demuxer_c(this);

      else if (0x00ff == codec_id)
        dmx = new ogm_a_aac_demuxer_c(this);

      else
        mxwarn_fn(ti.fname, boost::format(Y("Unknown audio stream type 0x%|1$04x|. Stream ID %2% will be ignored.\n")) % codec_id % sdemuxers.size());

    } else if (!strncmp(sth->streamtype, "text", 4))
      dmx = new ogm_s_text_demuxer_c(this);
  }

  /*
   * The old OggDS headers (see MPlayer's source, libmpdemux/demux_ogg.c)
   * are not supported.
   */

  if (!dmx)
    dmx = new ogm_demuxer_c(this);

  string type   = dmx->get_type();

  dmx->serialno = ogg_page_serialno(og);
  dmx->track_id = sdemuxers.size();
  dmx->in_use   = (type != "unknown") && demuxing_requested(type[0], dmx->track_id);

  dmx->packet_data.push_back(memory_cptr(new memory_c((unsigned char *)safememdup(op.packet, op.bytes), op.bytes, true)));

  memcpy(&dmx->os, &os, sizeof(ogg_stream_state));

  sdemuxers.push_back(ogm_demuxer_cptr(dmx));

  dmx->initialize();
}

/*
   Process the contents of a page. First find the demuxer associated with
   the page's serial number. If there is no such demuxer then either the
   OGG file is damaged (very rare) or the page simply belongs to a stream
   that the user didn't want extracted.
   If the demuxer is found then hand over all packets in this page to the
   associated packetizer.
*/
void
ogm_reader_c::process_page(ogg_page *og) {
  ogm_demuxer_cptr dmx;
  int64_t granulepos;

  dmx = find_demuxer(ogg_page_serialno(og));
  if (!dmx.get() || !dmx->in_use)
    return;

  granulepos = ogg_page_granulepos(og);

  if ((-1 != granulepos) && (granulepos < dmx->last_granulepos)) {
    mxwarn_tid(ti.fname, dmx->track_id,
               Y("The timecodes for this stream have been reset in the middle of the file. This is not supported. The current packet will be discarded.\n"));
    return;
  }

  ogg_stream_pagein(&dmx->os, og);

  dmx->process_page(granulepos);

  dmx->last_granulepos = granulepos;
}

void
ogm_reader_c::process_header_page(ogg_page *og) {
  ogm_demuxer_cptr dmx;

  dmx = find_demuxer(ogg_page_serialno(og));
  if (!dmx.get() ||dmx->headers_read)
    return;

  ogg_stream_pagein(&dmx->os, og);
  process_header_packets(dmx);
}

/*
   Search and store additional headers for the Ogg streams.
*/
void
ogm_reader_c::process_header_packets(ogm_demuxer_cptr dmx) {
  if (dmx->headers_read)
    return;

  dmx->process_header_page();
}

/*
   Read all header packets and - for Vorbis streams - the comment and
   codec data packets.
*/
int
ogm_reader_c::read_headers() {
  ogg_page og;

  bool done = false;
  while (!done) {
    // Make sure we have a page that we can work with.
    if (read_page(&og) == FILE_STATUS_DONE)
      return 0;

    // Is this the first page of a new stream?
    if (ogg_page_bos(&og))
      handle_new_stream_and_packets(&og);
    else {                // No, so check if it's still a header page.
      bos_pages_read = 1;
      process_header_page(&og);

      done = true;

      int i;
      for (i = 0; i < sdemuxers.size(); i++) {
        ogm_demuxer_cptr &dmx = sdemuxers[i];
        if (!dmx->headers_read && dmx->in_use) {
          done = false;
          break;
        }
      }
    }
  }

  io->setFilePointer(0, seek_beginning);
  ogg_sync_clear(&oy);
  ogg_sync_init(&oy);

  return 1;
}

/*
   General reader. Read a page and hand it over for processing.
*/
file_status_e
ogm_reader_c::read(generic_packetizer_c *,
                   bool) {
  // Some tracks may contain huge gaps. We don't want to suck in the complete
  // file.
  if (get_queued_bytes() > 20 * 1024 * 1024)
    return FILE_STATUS_HOLDING;

  ogg_page og;

  do {
    // Make sure we have a page that we can work with.
    if (read_page(&og) == FILE_STATUS_DONE) {
      flush_packetizers();
      return FILE_STATUS_DONE;
    }

    // Is this the first page of a new stream? No, so process it normally.
    if (!ogg_page_bos(&og))
      process_page(&og);
  } while (ogg_page_bos(&og));

  int i;
  // Are there streams that have not finished yet?
  for (i = 0; i < sdemuxers.size(); i++)
    if (!sdemuxers[i]->eos && sdemuxers[i]->in_use)
      return FILE_STATUS_MOREDATA;

  // No, we're done with this file.
  flush_packetizers();
  return FILE_STATUS_DONE;
}

int
ogm_reader_c::get_progress() {
  return (int)(io->getFilePointer() * 100 / file_size);
}

void
ogm_reader_c::identify() {
  vector<string> verbose_info;
  int i;

  // Check if a video track has a TITLE comment. If yes we use this as the
  // new segment title / global file title.
  for (i = 0; i < sdemuxers.size(); i++)
    if ((sdemuxers[i]->title != "") && (sdemuxers[i]->stype == OGM_STREAM_TYPE_V_MSCOMP)) {
      verbose_info.push_back(string("title:") + escape(sdemuxers[i]->title));
      break;
    }

  id_result_container("Ogg/OGM", join(" ", verbose_info));

  for (i = 0; i < sdemuxers.size(); i++) {
    verbose_info.clear();

    if (sdemuxers[i]->language != "")
      verbose_info.push_back(string("language:") + escape(sdemuxers[i]->language));

    if ((sdemuxers[i]->title != "") && (sdemuxers[i]->stype != OGM_STREAM_TYPE_V_MSCOMP))
      verbose_info.push_back(string("track_name:") + escape(sdemuxers[i]->title));

    if ((0 != sdemuxers[i]->display_width) && (0 != sdemuxers[i]->display_height))
      verbose_info.push_back((boost::format("display_dimensions:%1%x%2%") % sdemuxers[i]->display_width % sdemuxers[i]->display_height).str());

    id_result_track(i, sdemuxers[i]->get_type(), sdemuxers[i]->get_codec(), verbose_info);
  }
}

void
ogm_reader_c::handle_stream_comments() {
  counted_ptr<vector<string> > comments;
  string title;

  bool charset_warning_printed = false;
  charset_converter_cptr cch   = charset_converter_c::init(ti.chapter_charset);
  int i;

  for (i = 0; i < sdemuxers.size(); i++) {
    ogm_demuxer_cptr &dmx = sdemuxers[i];
    if ((OGM_STREAM_TYPE_A_FLAC == dmx->stype) || (2 > dmx->packet_data.size()))
      continue;

    comments = extract_vorbis_comments(dmx->packet_data[1]);
    if (comments->empty())
      continue;

    vector<string> chapter_strings;

    int j;
    for (j = 0; comments->size() > j; j++) {
      mxverb(2, boost::format("ogm_reader: commment for #%1% for %2%: %3%\n") % j % i % (*comments)[j]);
      vector<string> comment = split((*comments)[j], "=", 2);
      if (comment.size() != 2)
        continue;

      if (comment[0] == "LANGUAGE") {
        int index;

        index = map_to_iso639_2_code(comment[1].c_str());
        if (-1 != index)
          dmx->language = iso639_languages[index].iso639_2_code;
        else {

          string lang = comment[1];
          int pos1    = lang.find("[");
          while (0 <= pos1) {
            int pos2 = lang.find("]", pos1);
            if (-1 == pos2)
              pos2 = lang.length() - 1;
            lang.erase(pos1, pos2 - pos1 + 1);
            pos1 = lang.find("[");
          }

          pos1 = lang.find("(");
          while (0 <= pos1) {
            int pos2 = lang.find(")", pos1);
            if (-1 == pos2)
              pos2 = lang.length() - 1;
            lang.erase(pos1, pos2 - pos1 + 1);
            pos1 = lang.find("(");
          }

          index = map_to_iso639_2_code(lang.c_str());
          if (-1 != index)
            dmx->language = iso639_languages[index].iso639_2_code;
        }

      } else if (comment[0] == "TITLE")
        title = comment[1];

      else if (starts_with(comment[0], "CHAPTER"))
        chapter_strings.push_back((*comments)[j]);
    }

    bool segment_title_set = false;
    if (title != "") {
      title = cch->utf8(title);
      if (!g_segment_title_set && g_segment_title.empty() && (OGM_STREAM_TYPE_V_MSCOMP == dmx->stype)) {
        g_segment_title     = title;
        g_segment_title_set = true;
        segment_title_set   = true;
      }
      dmx->title = title.c_str();
      title      = "";
    }

    bool chapters_set = false;
    if (!chapter_strings.empty() && !ti.no_chapters && (NULL == g_kax_chapters)) {
      try {
        auto_ptr<mm_mem_io_c> out(new mm_mem_io_c(NULL, 0, 1000));

        out->write_bom("UTF-8");
        for (j = 0; j < chapter_strings.size(); j++)
          out->puts(cch->utf8(chapter_strings[j]) + string("\n"));
        out->set_file_name(ti.fname);

        auto_ptr<mm_text_io_c> text_out(new mm_text_io_c(out.get(), false));

        g_kax_chapters = parse_chapters(text_out.get());
        chapters_set   = true;
      } catch (...) {
      }
    }

    if (    (segment_title_set || chapters_set)
         && !charset_warning_printed
         && (ti.chapter_charset == "")) {
      mxwarn_fn(ti.fname,
                Y("This Ogg/OGM file contains chapter or title information. Unfortunately the charset used to store this information in "
                  "the file cannot be identified unambiguously. The program assumes that your system's current charset is appropriate. This can "
                  "be overridden with the '--chapter-charset <charset>' switch.\n"));
      charset_warning_printed = true;
    }
  }
}

void
ogm_reader_c::add_available_track_ids() {
  int i;

  for (i = 0; i < sdemuxers.size(); i++)
    available_track_ids.push_back(i);
}

// -----------------------------------------------------------

ogm_demuxer_c::ogm_demuxer_c(ogm_reader_c *p_reader)
  : reader(p_reader)
  , ptzr(-1)
  , stype(OGM_STREAM_TYPE_UNKNOWN)
  , serialno(0)
  , eos(0)
  , units_processed(0)
  , num_header_packets(2)
  , num_non_header_packets(0)
  , headers_read(false)
  , first_granulepos(0)
  , last_granulepos(0)
  , default_duration(0)
  , in_use(false)
  , display_width(0)
  , display_height(0)
{
  memset(&os, 0, sizeof(ogg_stream_state));
}

ogm_demuxer_c::~ogm_demuxer_c() {
  ogg_stream_clear(&os);
}

void
ogm_demuxer_c::get_duration_and_len(ogg_packet &op,
                                    int64_t &duration,
                                    int &duration_len) {
  duration_len  = (*op.packet & PACKET_LEN_BITS01) >> 6;
  duration_len |= (*op.packet & PACKET_LEN_BITS2)  << 1;

  duration      = 0;

  if ((0 < duration_len) && (op.bytes >= (duration_len + 1))) {
    for (int i = 0; i < duration_len; i++) {
      duration <<= 8;
      duration  += *((unsigned char *)op.packet + duration_len - i);
    }
  } else
    duration = 0;
}

void ogm_demuxer_c::process_page(int64_t granulepos) {
  ogg_packet op;

  while (ogg_stream_packetout(&os, &op) == 1) {
    eos |= op.e_o_s;

    if (((*op.packet & 3) == PACKET_TYPE_HEADER) || ((*op.packet & 3) == PACKET_TYPE_COMMENT))
      continue;

    int64_t duration;
    int duration_len;
    get_duration_and_len(op, duration, duration_len);

    memory_c *mem = new memory_c(&op.packet[duration_len + 1], op.bytes - 1 - duration_len, false);
    reader->reader_packetizers[ptzr]->process(new packet_t(mem));
    units_processed += op.bytes - 1;
  }
}

void
ogm_demuxer_c::process_header_page() {
  ogg_packet op;

  while (ogg_stream_packetout(&os, &op) == 1) {
    eos |= op.e_o_s;

    if (!is_header_packet(op)) {
      if (nh_packet_data.size() != num_non_header_packets) {
        nh_packet_data.push_back(clone_memory(op.packet, op.bytes));
        continue;
      }

      mxwarn_tid(reader->ti.fname, track_id,
                 Y("Missing header/comment packets for stream. This file is broken but should be muxed correctly. "
                   "If not please contact the author Moritz Bunkus <moritz@bunkus.org>.\n"));
      headers_read = true;
      ogg_stream_reset(&os);
      return;
    }

    packet_data.push_back(clone_memory(op.packet, op.bytes));

    eos |= op.e_o_s;
  }

  if (   (packet_data.size()    == num_header_packets)
      && (nh_packet_data.size() >= num_non_header_packets))
    headers_read = true;
}

// -----------------------------------------------------------

ogm_a_aac_demuxer_c::ogm_a_aac_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
{
  stype = OGM_STREAM_TYPE_A_AAC;
}

generic_packetizer_c *
ogm_a_aac_demuxer_c::create_packetizer(track_info_c &ti) {
  int profile, channels, sample_rate, output_sample_rate;
  stream_header *sth;
  bool sbr = false;

  if ((packet_data[0]->get_size() >= (sizeof(stream_header) + 5)) &&
      (parse_aac_data(packet_data[0]->get()      + sizeof(stream_header) + 5,
                      packet_data[0]->get_size() - sizeof(stream_header) - 5,
                      profile, channels, sample_rate, output_sample_rate, sbr))) {
    if (sbr)
      profile = AAC_PROFILE_SBR;

  } else {
    sth         = (stream_header *)&packet_data[0]->get()[1];
    channels    = get_uint16_le(&sth->sh.audio.channels);
    sample_rate = get_uint64_le(&sth->samples_per_unit);
    profile     = AAC_PROFILE_LC;
  }

  mxverb(2,
         boost::format("ogm_reader: %1%/%2%: profile %3%, channels %4%, sample_rate %5%, sbr %6%, output_sample_rate %7%\n")
         % ti.id % ti.fname % profile % channels % sample_rate % sbr % output_sample_rate);

  generic_packetizer_c *ptzr_obj = new aac_packetizer_c(reader, ti, AAC_ID_MPEG4, profile, sample_rate, channels, false, true);
  if (sbr)
    ptzr_obj->set_audio_output_sampling_freq(output_sample_rate);

  mxinfo_tid(ti.fname, ti.id,  Y("Using the AAC output module.\n"));

  return ptzr_obj;
}

// -----------------------------------------------------------

ogm_a_ac3_demuxer_c::ogm_a_ac3_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
{
  stype = OGM_STREAM_TYPE_A_AC3;
}

generic_packetizer_c *
ogm_a_ac3_demuxer_c::create_packetizer(track_info_c &ti) {
  stream_header        *sth      = (stream_header *)(packet_data[0]->get() + 1);
  generic_packetizer_c *ptzr_obj = new ac3_packetizer_c(reader, ti, get_uint64_le(&sth->samples_per_unit), get_uint16_le(&sth->sh.audio.channels), 0);

  mxinfo_tid(ti.fname, ti.id, Y("Using the AC3 output module.\n"));

  return ptzr_obj;
}

// -----------------------------------------------------------

ogm_a_mp3_demuxer_c::ogm_a_mp3_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
{
  stype = OGM_STREAM_TYPE_A_MP3;
}

generic_packetizer_c *
ogm_a_mp3_demuxer_c::create_packetizer(track_info_c &ti) {
  stream_header        *sth      = (stream_header *)(packet_data[0]->get() + 1);
  generic_packetizer_c *ptzr_obj = new mp3_packetizer_c(reader, ti, get_uint64_le(&sth->samples_per_unit), get_uint16_le(&sth->sh.audio.channels), true);

  mxinfo_tid(ti.fname, ti.id, Y("Using the MPEG audio output module.\n"));

  return ptzr_obj;
}

// -----------------------------------------------------------

ogm_a_pcm_demuxer_c::ogm_a_pcm_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
{
  stype = OGM_STREAM_TYPE_A_PCM;
}

generic_packetizer_c *
ogm_a_pcm_demuxer_c::create_packetizer(track_info_c &ti) {
  stream_header        *sth      = (stream_header *)(packet_data[0]->get() + 1);
  generic_packetizer_c *ptzr_obj = new pcm_packetizer_c(reader, ti, get_uint64_le(&sth->samples_per_unit), get_uint16_le(&sth->sh.audio.channels),
                                                        get_uint16_le(&sth->bits_per_sample));

  mxinfo_tid(ti.fname, ti.id, Y("Using the PCM output module.\n"));

  return ptzr_obj;
}

// -----------------------------------------------------------

ogm_a_vorbis_demuxer_c::ogm_a_vorbis_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
{
  stype              = OGM_STREAM_TYPE_A_VORBIS;
  num_header_packets = 3;
}

generic_packetizer_c *
ogm_a_vorbis_demuxer_c::create_packetizer(track_info_c &ti) {
  generic_packetizer_c *ptzr_obj = new vorbis_packetizer_c(reader, ti,
                                                           packet_data[0]->get(), packet_data[0]->get_size(),
                                                           packet_data[1]->get(), packet_data[1]->get_size(),
                                                           packet_data[2]->get(), packet_data[2]->get_size());

  mxinfo_tid(ti.fname, ti.id, Y("Using the Vorbis output module.\n"));

  return ptzr_obj;
}

void
ogm_a_vorbis_demuxer_c::process_page(int64_t granulepos) {
  ogg_packet op;

  while (ogg_stream_packetout(&os, &op) == 1) {
    eos |= op.e_o_s;

    if (((*op.packet & 3) == PACKET_TYPE_HEADER) || ((*op.packet & 3) == PACKET_TYPE_COMMENT))
      continue;

    reader->reader_packetizers[ptzr]->process(new packet_t(new memory_c(op.packet, op.bytes, false)));
  }
}

// -----------------------------------------------------------

ogm_s_text_demuxer_c::ogm_s_text_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
{
  stype = OGM_STREAM_TYPE_A_AAC;
}

generic_packetizer_c *
ogm_s_text_demuxer_c::create_packetizer(track_info_c &ti) {
  generic_packetizer_c *ptzr_obj = new textsubs_packetizer_c(reader, ti, MKV_S_TEXTUTF8, NULL, 0, true, false);

  mxinfo_tid(ti.fname, ti.id, Y("Using the text subtitle output module.\n"));

  return ptzr_obj;
}

void
ogm_s_text_demuxer_c::process_page(int64_t granulepos) {
  ogg_packet op;
  int duration_len;
  int64_t duration;

  units_processed++;

  while (ogg_stream_packetout(&os, &op) == 1) {
    eos |= op.e_o_s;

    if (((*op.packet & 3) == PACKET_TYPE_HEADER) || ((*op.packet & 3) == PACKET_TYPE_COMMENT))
      continue;

    get_duration_and_len(op, duration, duration_len);

    if (((op.bytes - 1 - duration_len) > 2) || ((op.packet[duration_len + 1] != ' ') && (op.packet[duration_len + 1] != 0) && !iscr(op.packet[duration_len + 1]))) {
      memory_c *mem = new memory_c(&op.packet[duration_len + 1], op.bytes - 1 - duration_len, false);
      reader->reader_packetizers[ptzr]->process(new packet_t(mem, granulepos * 1000000, (int64_t)duration * 1000000));
    }
  }
}

// -----------------------------------------------------------

ogm_v_avc_demuxer_c::ogm_v_avc_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
{
  stype                  = OGM_STREAM_TYPE_V_AVC;
  num_non_header_packets = 3;
}

void
ogm_v_avc_demuxer_c::initialize() {
  stream_header *sth = (stream_header *)(packet_data[0]->get() + 1);

  if (0 > g_video_fps)
    g_video_fps = 10000000.0 / (float)get_uint64_le(&sth->time_unit);
}

generic_packetizer_c *
ogm_v_avc_demuxer_c::create_packetizer(track_info_c &ti) {
  mpeg4_p10_es_video_packetizer_c *vptzr = NULL;

  try {
    stream_header *sth = (stream_header *)&packet_data[0]->get()[1];

    ti.private_data    = NULL;
    ti.private_size    = 0;
    memory_cptr avcc   = extract_avcc();

    vptzr              = new mpeg4_p10_es_video_packetizer_c(reader, ti, avcc, get_uint32_le(&sth->sh.video.width), get_uint32_le(&sth->sh.video.height));

    vptzr->enable_timecode_generation(false);
    vptzr->set_track_default_duration(default_duration);

    mxinfo_tid(ti.fname, ti.id, Y("Using the MPEG-4 part 10 ES video output module.\n"));

  } catch (...) {
    mxerror_tid(ti.fname, ti.id, Y("Could not extract the decoder specific config data (AVCC) from this AVC/h.264 track.\n"));
  }

  return vptzr;
}

memory_cptr
ogm_v_avc_demuxer_c::extract_avcc() {
  avc_es_parser_c parser;

  parser.ignore_nalu_size_length_errors();
  if (map_has_key(reader->ti.nalu_size_lengths, track_id))
    parser.set_nalu_size_length(reader->ti.nalu_size_lengths[track_id]);
  else if (map_has_key(reader->ti.nalu_size_lengths, -1))
    parser.set_nalu_size_length(reader->ti.nalu_size_lengths[-1]);

  unsigned char *private_data = packet_data[0]->get()      + 1 + sizeof(stream_header);
  int private_size            = packet_data[0]->get_size() - 1 - sizeof(stream_header);

  while (4 < private_size) {
    if (get_uint32_be(private_data) == 0x00000001) {
      parser.add_bytes(private_data, private_size);
      break;
    }

    ++private_data;
    --private_size;
  }

  vector<memory_cptr>::iterator packet(nh_packet_data.begin());

  while (packet != nh_packet_data.end()) {
    if ((*packet)->get_size()) {
      parser.add_bytes((*packet)->get(), (*packet)->get_size());
      if (parser.headers_parsed())
        return parser.get_avcc();
    }

    ++packet;
  }

  throw false;
}

// -----------------------------------------------------------

ogm_v_mscomp_demuxer_c::ogm_v_mscomp_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
  , frames_since_granulepos_change(0)
{
  stype = OGM_STREAM_TYPE_V_MSCOMP;
}

string
ogm_v_mscomp_demuxer_c::get_codec() {
  stream_header *sth = (stream_header *)(packet_data[0]->get() + 1);
  char fourcc[5];

  memcpy(fourcc, sth->subtype, 4);
  fourcc[4] = 0;

  return fourcc;
}

void
ogm_v_mscomp_demuxer_c::initialize() {
  stream_header *sth = (stream_header *)(packet_data[0]->get() + 1);

  if (0 > g_video_fps)
    g_video_fps = 10000000.0 / (float)get_uint64_le(&sth->time_unit);

  default_duration = 100 * get_uint64_le(&sth->time_unit);
}

generic_packetizer_c *
ogm_v_mscomp_demuxer_c::create_packetizer(track_info_c &ti) {
  alBITMAPINFOHEADER bih;
  stream_header *sth = (stream_header *)&packet_data[0]->get()[1];

  // AVI compatibility mode. Fill in the values we've got and guess
  // the others.
  memset(&bih, 0, sizeof(alBITMAPINFOHEADER));
  put_uint32_le(&bih.bi_size,       sizeof(alBITMAPINFOHEADER));
  put_uint32_le(&bih.bi_width,      get_uint32_le(&sth->sh.video.width));
  put_uint32_le(&bih.bi_height,     get_uint32_le(&sth->sh.video.height));
  put_uint16_le(&bih.bi_planes,     1);
  put_uint16_le(&bih.bi_bit_count,  24);
  put_uint32_le(&bih.bi_size_image, get_uint32_le(&bih.bi_width) * get_uint32_le(&bih.bi_height) * 3);
  memcpy(&bih.bi_compression, sth->subtype, 4);

  ti.private_data  = (unsigned char *)&bih;
  ti.private_size  = sizeof(alBITMAPINFOHEADER);

  double fps       = (double)10000000.0 / get_uint64_le(&sth->time_unit);
  int width        = get_uint32_le(&sth->sh.video.width);
  int height       = get_uint32_le(&sth->sh.video.height);

  generic_packetizer_c *ptzr_obj;
  if (mpeg4::p2::is_fourcc(sth->subtype)) {
    ptzr_obj = new mpeg4_p2_video_packetizer_c(reader, ti, fps, width, height, false);

    mxinfo_tid(ti.fname, ti.id, Y("Using the MPEG-4 part 2 video output module.\n"));

  } else {
    ptzr_obj = new video_packetizer_c(reader, ti, NULL, fps, width, height);

    mxinfo_tid(ti.fname, ti.id, Y("Using the video output module.\n"));
  }

  ti.private_data = NULL;

  return ptzr_obj;
}

void
ogm_v_mscomp_demuxer_c::process_page(int64_t granulepos) {
  vector<ogm_frame_t> frames;
  ogg_packet op;
  int64_t duration;

  while (ogg_stream_packetout(&os, &op) == 1) {
    eos |= op.e_o_s;

    if (((*op.packet & 3) == PACKET_TYPE_HEADER) || ((*op.packet & 3) == PACKET_TYPE_COMMENT))
      continue;

    int duration_len;
    get_duration_and_len(op, duration, duration_len);

    if (!duration_len || !duration)
      duration = 1;

    ogm_frame_t frame = {
      new memory_c(&op.packet[duration_len + 1], op.bytes - 1 - duration_len, false),
      duration * default_duration,
      op.packet[0],
    };

    frames.push_back(frame);
  }

  // Is there a gap in the granulepos values?
  if ((granulepos - last_granulepos) > frames.size())
    last_granulepos = granulepos - frames.size();

  int i;
  for (i = 0; i < frames.size(); ++i) {
    ogm_frame_t &frame = frames[i];

    int64_t timecode = (last_granulepos + frames_since_granulepos_change) * default_duration;
    ++frames_since_granulepos_change;

    reader->reader_packetizers[ptzr]->process(new packet_t(frame.mem, timecode, frame.duration, frame.flags & PACKET_IS_SYNCPOINT ? VFT_IFRAME : VFT_PFRAMEAUTOMATIC));

    units_processed += duration;
  }

  if (granulepos != last_granulepos)
    frames_since_granulepos_change = 0;
}

// -----------------------------------------------------------

ogm_v_theora_demuxer_c::ogm_v_theora_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
{
  stype              = OGM_STREAM_TYPE_V_THEORA;
  num_header_packets = 3;

  memset(&theora, 0, sizeof(theora_identification_header_t));
}

void
ogm_v_theora_demuxer_c::initialize() {
  try {
    memory_cptr &mem = packet_data[0];
    theora_parse_identification_header(mem->get(), mem->get_size(), theora);

    display_width  = theora.display_width;
    display_height = theora.display_height;
  } catch (error_c &e) {
    mxerror_tid(reader->ti.fname, track_id, boost::format(Y("The Theora identifaction header could not be parsed (%1%).\n")) % e.get_error());
  }
}

generic_packetizer_c *
ogm_v_theora_demuxer_c::create_packetizer(track_info_c &ti) {
  memory_cptr codecprivate       = lace_memory_xiph(packet_data);
  ti.private_data                = codecprivate->get();
  ti.private_size                = codecprivate->get_size();

  double                fps      = (double)theora.frn / (double)theora.frd;
  generic_packetizer_c *ptzr_obj = new theora_video_packetizer_c(reader, ti, fps, theora.fmbw, theora.fmbh);

  mxinfo_tid(ti.fname, ti.id, Y("Using the Theora video output module.\n"));

  ti.private_data = NULL;

  return ptzr_obj;
}

void
ogm_v_theora_demuxer_c::process_page(int64_t granulepos) {
  ogg_packet op;

  while (ogg_stream_packetout(&os, &op) == 1) {
    eos |= op.e_o_s;

    if ((0 == op.bytes) || (0 != (op.packet[0] & 0x80)))
      continue;

    bool is_keyframe = 0x00 == (op.packet[0] & 0x40);
    int64_t timecode = (int64_t)(1000000000.0 * units_processed * theora.frd / theora.frn);
    int64_t duration = (int64_t)(1000000000.0 *                   theora.frd / theora.frn);
    int64_t bref     = is_keyframe ? VFT_IFRAME : VFT_PFRAMEAUTOMATIC;

    ++units_processed;

    reader->reader_packetizers[ptzr]->process(new packet_t(new memory_c(op.packet, op.bytes, false), timecode, duration, bref, VFT_NOBFRAME));

    mxverb(3,
           boost::format("Theora track %1% kfgshift %2% granulepos 0x%|3$08x| %|4$08x|%5%\n")
           % track_id % theora.kfgshift % (granulepos >> 32) % (granulepos & 0xffffffff) % (is_keyframe ? " key" : ""));
  }
}

bool
ogm_v_theora_demuxer_c::is_header_packet(ogg_packet &op) {
  return ((0x80 <= op.packet[0]) && (0x82 >= op.packet[0]));
}

// -----------------------------------------------------------

ogm_s_kate_demuxer_c::ogm_s_kate_demuxer_c(ogm_reader_c *p_reader)
  : ogm_demuxer_c(p_reader)
{
  stype              = OGM_STREAM_TYPE_S_KATE;
  num_header_packets = 1; /* at least 1, will be updated upon reading the ID header */

  memset(&kate, 0, sizeof(kate_identification_header_t));
}

void
ogm_s_kate_demuxer_c::initialize() {
  try {
    memory_cptr &mem = packet_data[0];
    kate_parse_identification_header(mem->get(), mem->get_size(), kate);
    num_header_packets = kate.nheaders;
  } catch (error_c &e) {
    mxerror_tid(reader->ti.fname, track_id, boost::format(Y("The Kate identifaction header could not be parsed (%1%).\n")) % e.get_error());
  }
}

generic_packetizer_c *
ogm_s_kate_demuxer_c::create_packetizer(track_info_c &ti) {
  memory_cptr codecprivate       = lace_memory_xiph(packet_data);
  ti.private_data                = codecprivate->get();
  ti.private_size                = codecprivate->get_size();

  generic_packetizer_c *ptzr_obj = new kate_packetizer_c(reader, ti, ti.private_data, ti.private_size);

  mxinfo_tid(ti.fname, ti.id, Y("Using the Kate subtitle output module.\n"));

  ti.private_data = NULL;

  return ptzr_obj;
}

void
ogm_s_kate_demuxer_c::process_page(int64_t granulepos) {
  ogg_packet op;

  while (ogg_stream_packetout(&os, &op) == 1) {
    eos |= op.e_o_s;

    if ((0 == op.bytes) || (0 != (op.packet[0] & 0x80)))
      continue;

    reader->reader_packetizers[ptzr]->process(new packet_t(new memory_c(op.packet, op.bytes, false)));

    ++units_processed;

    if (op.e_o_s) {
      eos = 1;
      return;
    }
  }
}

bool
ogm_s_kate_demuxer_c::is_header_packet(ogg_packet &op) {
  return (0x80 & op.packet[0]);
}
