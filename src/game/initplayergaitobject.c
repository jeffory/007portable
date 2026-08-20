#include <ultra64.h>
#include "chrobjdata.h"

void init_player_gait_object(void) {
  /* was (int)& — truncated the static's address under PIE */
  player_gait_object_header.RootNode = (struct ModelNode *)&player_gait_hdr;
  return;
}

