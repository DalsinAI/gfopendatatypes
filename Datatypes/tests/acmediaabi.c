/* Compile/link check for the public ACMedia datatype messages on AmigaOS. */
#include <stddef.h>
#include <datatypes/acmediaclass.h>
#include "amcore.h"
#define ASSERT(name, x) typedef char assert_##name[(x) ? 1 : -1]
ASSERT(time_8, sizeof(struct amdtTime) == 8);
ASSERT(rect_16, sizeof(struct amdtRect) == 16);
ASSERT(overlay_28, sizeof(struct amdtOverlay) == 28);
ASSERT(addtrack_16, sizeof(struct amdtAddTrack) == 16);
ASSERT(feed_76, sizeof(struct amdtFeed) == 76);
ASSERT(feed_data_16, offsetof(struct amdtFeed, amf_Data) == 16);
ASSERT(feed_start_36, offsetof(struct amdtFeed, amf_Start) == 36);
ASSERT(remove_28, sizeof(struct amdtRemove) == 28);
ASSERT(seek_20, sizeof(struct amdtSeek) == 20);
ASSERT(end_16, sizeof(struct amdtEnd) == 16);
ASSERT(event_20, sizeof(struct amdtEvent) == 20);
int main(void)
{
    struct amd_core c;
    amd_init(&c, 0, 1, 0, 1);
    amd_free(&c);
    return c.bytes != 0;
}
