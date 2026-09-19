#ifndef IMPEGHD_RAWDUMP_H
#define IMPEGHD_RAWDUMP_H

#include <stdio.h>
#include <impeghd_type_def.h>
#include "impeghd_memory_standards.h"

#define IMPEGHD_RAWDUMP_MAX_CHANNELS 24

typedef struct
{
  FILE *channels[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  FILE *objects[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  FILE *hoa[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  FILE *rendered[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  unsigned long channel_bytes[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  unsigned long object_bytes[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  unsigned long hoa_bytes[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  unsigned long rendered_bytes[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  WORD32 transport_sample_rate;
  WORD32 rendered_sample_rate;
  WORD32 rendered_bits;
  WORD32 rendered_channels;
  WORD32 num_channels;
  WORD32 num_objects;
  WORD32 num_hoa;
  WORD32 obj_metadata_seen;
  WORD32 obj_movement[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  FLOAT32 obj_first_azimuth[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  FLOAT32 obj_first_elevation[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  FLOAT32 obj_first_radius[IMPEGHD_RAWDUMP_MAX_CHANNELS];
  char root[300];
} impeghd_rawdump;

IA_ERRORCODE impeghd_rawdump_open(impeghd_rawdump *d, const char *root,
                                  const ia_output_config *cfg);
IA_ERRORCODE impeghd_rawdump_write_transport(impeghd_rawdump *d,
                                             const UWORD8 *pcm,
                                             const ia_output_config *cfg);
IA_ERRORCODE impeghd_rawdump_write_rendered(impeghd_rawdump *d,
                                            const UWORD8 *pcm,
                                            WORD32 bytes);
VOID impeghd_rawdump_close(impeghd_rawdump *d);

#endif
