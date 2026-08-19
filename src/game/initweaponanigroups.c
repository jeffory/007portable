#include <ultra64.h>
#include "model.h"

//uncomment when actor is worked on
//#include "chr.h"
extern s32 get_ptr_allocated_block_for_vertices;

void init_weapon_animation_groups_maybe(void) {
    set_vtxallocator(&get_ptr_allocated_block_for_vertices);
    /* also rebases the weapon/hit-reaction anim tables from raw PTR_ANIM_
     * offsets to real pointers — guards crash without it. The M2-era parse
     * failures were bad generated ANIM_DATA_ offsets, since fixed. */
    initWeaponAnimGroups();
}
