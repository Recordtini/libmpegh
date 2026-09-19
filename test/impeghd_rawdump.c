#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define RAW_MKDIR(p) _mkdir(p)
#define RAW_SEP "\\"
#else
#include <sys/stat.h>
#include <sys/types.h>
#define RAW_MKDIR(p) mkdir((p), 0777)
#define RAW_SEP "/"
#endif

#include <impeghd_type_def.h>
#include "impeghd_rawdump.h"

static VOID raw_put16(FILE *fp, WORD32 v)
{
  putc(v & 255, fp);
  putc((v >> 8) & 255, fp);
}

static VOID raw_put32(FILE *fp, unsigned long v)
{
  raw_put16(fp, (WORD32)(v & 0xffff));
  raw_put16(fp, (WORD32)((v >> 16) & 0xffff));
}

static VOID raw_wav_header(FILE *fp, unsigned long pcm_bytes, WORD32 fs, WORD32 bits)
{
  WORD32 bytes_per_sample = (bits + 7) / 8;
  fwrite("RIFF", 1, 4, fp);
  raw_put32(fp, pcm_bytes + 36);
  fwrite("WAVEfmt ", 1, 8, fp);
  raw_put32(fp, 16);
  raw_put16(fp, 1);
  raw_put16(fp, 1);
  raw_put32(fp, (unsigned long)fs);
  raw_put32(fp, (unsigned long)(fs * bytes_per_sample));
  raw_put16(fp, bytes_per_sample);
  raw_put16(fp, bits);
  fwrite("data", 1, 4, fp);
  raw_put32(fp, pcm_bytes);
}

static WORD32 raw_make_dir(const char *path)
{
  if (RAW_MKDIR(path) == 0 || errno == EEXIST)
    return 0;
  return -1;
}

static FILE *raw_open_mono(const char *dir, const char *prefix, WORD32 index,
                           WORD32 sample_rate, WORD32 bits)
{
  char path[420];
  FILE *fp;
  snprintf(path, sizeof(path), "%s%s%s_%02d.wav", dir, RAW_SEP, prefix, index);
  fp = fopen(path, "wb");
  if (fp != NULL)
    raw_wav_header(fp, 0, sample_rate, bits);
  return fp;
}

static WORD32 raw_clamp_count(WORD32 n)
{
  if (n < 0)
    return 0;
  if (n > IMPEGHD_RAWDUMP_MAX_CHANNELS)
    return IMPEGHD_RAWDUMP_MAX_CHANNELS;
  return n;
}

static VOID raw_write_manifest(impeghd_rawdump *d, const ia_output_config *cfg)
{
  char path[420];
  FILE *fp;
  WORD32 i;

  snprintf(path, sizeof(path), "%s%smanifest.json", d->root, RAW_SEP);
  fp = fopen(path, "wb");
  if (fp == NULL)
    return;

  fprintf(fp, "{\n");
  fprintf(fp, "  \"transport_sample_rate\": %d,\n", d->transport_sample_rate);
  fprintf(fp, "  \"transport_bit_depth\": 24,\n");
  fprintf(fp, "  \"transport_frame_samples\": %d,\n", cfg->ext_pcm_frame_samples);
  fprintf(fp, "  \"transport\": {\n");
  fprintf(fp, "    \"channel_signals\": %d,\n", d->num_channels);
  fprintf(fp, "    \"objects\": %d,\n", d->num_objects);
  fprintf(fp, "    \"hoa_transport_channels\": %d,\n", d->num_hoa);
  fprintf(fp, "    \"object_offset\": %d,\n", cfg->oam_sample_offset);
  fprintf(fp, "    \"hoa_end_offset\": %d\n", cfg->hoa_sample_offset);
  fprintf(fp, "  },\n");
  fprintf(fp, "  \"object_details\": [\n");
  for (i = 0; i < d->num_objects; i++)
  {
    WORD32 transport_index = cfg->oam_sample_offset + i;
    fprintf(fp,
            "    {\"index\": %d, \"transport_index\": %d, "
            "\"file\": \"objects/object_%02d.wav\", "
            "\"metadata_valid\": %d, \"position_fixed\": %d, "
            "\"movement\": %d, "
            "\"azimuth\": %.6f, \"elevation\": %.6f, "
            "\"radius\": %.6f, \"gain\": %.6f, "
            "\"spread_width\": %.6f, \"spread_height\": %.6f, "
            "\"spread_depth\": %.6f}%s\n",
            i, transport_index, i,
            cfg->obj_metadata_valid, cfg->obj_position_fixed,
            d->obj_movement[i],
            cfg->obj_azimuth[i], cfg->obj_elevation[i],
            cfg->obj_radius[i], cfg->obj_gain[i],
            cfg->obj_spread_width[i], cfg->obj_spread_height[i],
            cfg->obj_spread_depth[i],
            (i + 1 < d->num_objects) ? "," : "");
  }
  fprintf(fp, "  ],\n");
  fprintf(fp, "  \"rendered_sample_rate\": %d,\n", d->rendered_sample_rate);
  fprintf(fp, "  \"rendered_bit_depth\": %d,\n", d->rendered_bits);
  fprintf(fp, "  \"rendered_channels\": [\n");
  for (i = 0; i < d->rendered_channels; i++)
  {
    WORD32 az = (i < cfg->num_speakers) ? cfg->azimuth[i] : 0;
    WORD32 el = (i < cfg->num_speakers) ? cfg->elevation[i] : 0;
    WORD32 lfe = (i < cfg->num_speakers) ? cfg->is_lfe[i] : 0;
    fprintf(fp,
            "    {\"index\": %d, \"file\": \"rendered/speaker_%02d.wav\", "
            "\"azimuth\": %d, \"elevation\": %d, \"lfe\": %d}%s\n",
            i, i, az, el, lfe, (i + 1 < d->rendered_channels) ? "," : "");
  }
  fprintf(fp, "  ]\n");
  fprintf(fp, "}\n");
  fclose(fp);
}

IA_ERRORCODE impeghd_rawdump_open(impeghd_rawdump *d, const char *root,
                                  const ia_output_config *cfg)
{
  WORD32 i;
  char path[360];

  memset(d, 0, sizeof(*d));
  strncpy(d->root, root, sizeof(d->root) - 1);
  d->root[sizeof(d->root) - 1] = '\0';

  d->transport_sample_rate =
      (cfg->ext_pcm_sample_rate > 0) ? cfg->ext_pcm_sample_rate : cfg->i_samp_freq;
  d->rendered_sample_rate = cfg->i_samp_freq;
  d->rendered_bits = cfg->i_pcm_wd_sz;
  d->rendered_channels = raw_clamp_count(cfg->i_num_chan);
  d->num_channels = raw_clamp_count(cfg->ext_num_channel_signals);
  d->num_objects = raw_clamp_count(cfg->ext_num_objects);
  d->num_hoa = raw_clamp_count(cfg->ext_num_hoa_transport_channels);

  if (cfg->obj_metadata_valid)
  {
    d->obj_metadata_seen = 1;
    for (i = 0; i < d->num_objects; i++)
    {
      d->obj_first_azimuth[i] = cfg->obj_azimuth[i];
      d->obj_first_elevation[i] = cfg->obj_elevation[i];
      d->obj_first_radius[i] = cfg->obj_radius[i];
    }
  }

  if (raw_make_dir(root) != 0)
    return -1;

  snprintf(path, sizeof(path), "%s%schannels", root, RAW_SEP);
  if (raw_make_dir(path) != 0)
    return -1;
  for (i = 0; i < d->num_channels; i++)
  {
    d->channels[i] = raw_open_mono(path, "channel", i, d->transport_sample_rate, 24);
    if (d->channels[i] == NULL)
      return -1;
  }

  snprintf(path, sizeof(path), "%s%sobjects", root, RAW_SEP);
  if (raw_make_dir(path) != 0)
    return -1;
  for (i = 0; i < d->num_objects; i++)
  {
    d->objects[i] = raw_open_mono(path, "object", i, d->transport_sample_rate, 24);
    if (d->objects[i] == NULL)
      return -1;
  }

  snprintf(path, sizeof(path), "%s%shoa_transport", root, RAW_SEP);
  if (raw_make_dir(path) != 0)
    return -1;
  for (i = 0; i < d->num_hoa; i++)
  {
    d->hoa[i] = raw_open_mono(path, "hoa_transport", i, d->transport_sample_rate, 24);
    if (d->hoa[i] == NULL)
      return -1;
  }

  snprintf(path, sizeof(path), "%s%srendered", root, RAW_SEP);
  if (raw_make_dir(path) != 0)
    return -1;
  for (i = 0; i < d->rendered_channels; i++)
  {
    d->rendered[i] = raw_open_mono(path, "speaker", i, d->rendered_sample_rate,
                                   d->rendered_bits);
    if (d->rendered[i] == NULL)
      return -1;
  }

  raw_write_manifest(d, cfg);
  return 0;
}

IA_ERRORCODE impeghd_rawdump_write_transport(impeghd_rawdump *d,
                                             const UWORD8 *pcm,
                                             const ia_output_config *cfg)
{
  WORD32 sample, ch;
  WORD32 total = cfg->ext_pcm_num_channels;
  WORD32 samples;

  if (pcm == NULL || total <= 0)
    return 0;

  samples = cfg->pcm_payload_length / (3 * total);
  if (cfg->ext_pcm_frame_samples > 0 && samples > cfg->ext_pcm_frame_samples)
    samples = cfg->ext_pcm_frame_samples;

  if (cfg->obj_metadata_valid && d->num_objects > 0)
  {
    WORD32 i;
    WORD32 manifest_changed = 0;

    if (!d->obj_metadata_seen)
    {
      d->obj_metadata_seen = 1;
      for (i = 0; i < d->num_objects; i++)
      {
        d->obj_first_azimuth[i] = cfg->obj_azimuth[i];
        d->obj_first_elevation[i] = cfg->obj_elevation[i];
        d->obj_first_radius[i] = cfg->obj_radius[i];
      }
      manifest_changed = 1;
    }
    else
    {
      for (i = 0; i < d->num_objects; i++)
      {
        if (!d->obj_movement[i] &&
            (fabsf(cfg->obj_azimuth[i] - d->obj_first_azimuth[i]) > 0.05f ||
             fabsf(cfg->obj_elevation[i] - d->obj_first_elevation[i]) > 0.05f ||
             fabsf(cfg->obj_radius[i] - d->obj_first_radius[i]) > 0.001f))
        {
          d->obj_movement[i] = 1;
          manifest_changed = 1;
        }
      }
    }

    if (manifest_changed)
      raw_write_manifest(d, cfg);
  }

  {
    WORD32 object_start = cfg->oam_sample_offset;
    WORD32 hoa_start = cfg->hoa_sample_offset - d->num_hoa;

    /* Object and HOA blocks can appear in either order. Use the decoder's
       offsets instead of assuming channels -> objects -> HOA. */
    if (d->num_objects > 0 &&
        (object_start < 0 || object_start + d->num_objects > total))
      object_start = d->num_channels;

    if (d->num_hoa > 0 &&
        (hoa_start < 0 || hoa_start + d->num_hoa > total))
    {
      hoa_start = d->num_channels;
      if (d->num_objects > 0 && hoa_start == object_start)
        hoa_start += d->num_objects;
    }

    for (sample = 0; sample < samples; sample++)
    {
      for (ch = 0; ch < total; ch++)
      {
        const UWORD8 *src = pcm + 3 * (sample * total + ch);
        FILE *fp = NULL;
        unsigned long *count = NULL;

        if (ch < d->num_channels)
        {
          fp = d->channels[ch];
          count = &d->channel_bytes[ch];
        }
        else if (d->num_objects > 0 &&
                 ch >= object_start && ch < object_start + d->num_objects)
        {
          WORD32 object_index = ch - object_start;
          fp = d->objects[object_index];
          count = &d->object_bytes[object_index];
        }
        else if (d->num_hoa > 0 &&
                 ch >= hoa_start && ch < hoa_start + d->num_hoa)
        {
          WORD32 hoa_index = ch - hoa_start;
          fp = d->hoa[hoa_index];
          count = &d->hoa_bytes[hoa_index];
        }

        if (fp != NULL && count != NULL)
        {
          fwrite(src, 1, 3, fp);
          *count += 3;
        }
      }
    }
  }
  return 0;
}

IA_ERRORCODE impeghd_rawdump_write_rendered(impeghd_rawdump *d,
                                            const UWORD8 *pcm,
                                            WORD32 bytes)
{
  WORD32 sample, ch;
  WORD32 bytes_per_sample;
  WORD32 frames;

  if (pcm == NULL || d->rendered_channels <= 0 || d->rendered_bits <= 0)
    return 0;

  bytes_per_sample = (d->rendered_bits + 7) / 8;
  frames = bytes / (bytes_per_sample * d->rendered_channels);

  for (sample = 0; sample < frames; sample++)
  {
    for (ch = 0; ch < d->rendered_channels; ch++)
    {
      const UWORD8 *src =
          pcm + bytes_per_sample * (sample * d->rendered_channels + ch);
      fwrite(src, 1, bytes_per_sample, d->rendered[ch]);
      d->rendered_bytes[ch] += bytes_per_sample;
    }
  }
  return 0;
}

static VOID raw_finish_file(FILE **fp, unsigned long bytes,
                            WORD32 sample_rate, WORD32 bits)
{
  if (*fp == NULL)
    return;
  if (fseek(*fp, 0, SEEK_SET) == 0)
    raw_wav_header(*fp, bytes, sample_rate, bits);
  fclose(*fp);
  *fp = NULL;
}

VOID impeghd_rawdump_close(impeghd_rawdump *d)
{
  WORD32 i;
  for (i = 0; i < d->num_channels; i++)
    raw_finish_file(&d->channels[i], d->channel_bytes[i], d->transport_sample_rate, 24);
  for (i = 0; i < d->num_objects; i++)
    raw_finish_file(&d->objects[i], d->object_bytes[i], d->transport_sample_rate, 24);
  for (i = 0; i < d->num_hoa; i++)
    raw_finish_file(&d->hoa[i], d->hoa_bytes[i], d->transport_sample_rate, 24);
  for (i = 0; i < d->rendered_channels; i++)
    raw_finish_file(&d->rendered[i], d->rendered_bytes[i],
                    d->rendered_sample_rate, d->rendered_bits);
}
