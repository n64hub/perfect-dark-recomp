// The light and sun glares' depth test where render to RAM's copy into RDRAM is a display list late, as on the page.
//
// A glare shows where the z-buffer under its light holds the light's own depth (src/game/artifact.c's header): each
// display list copies the z-buffer under the lights it tests into a buffer of depth samples, and when the RDP is done
// with the list the scheduler (sched_update_pending_artifacts) keeps the nearer of each sample and the z-buffer, both
// read from RDRAM, for the frame that draws those glares two frames later. RT64 draws on the GPU and copies a list's
// images into RDRAM, and on the page that copy is late (src/main.cpp): it lands while the next list is drawn, after the
// scheduler has read. So the scheduler read the samples of the list three before, from the same one of the game's three
// buffers, taken where the lights were three frames earlier: they match while Joanna stands still, and as she walks
// towards a light or away from it the light's depth moves past the test's tolerance and its glare flickers.
//
// Where the copy is late (recomp_pd_render_to_ram_late) the game here keeps four lists of glares and four sample
// buffers, the fourth of each in the patches' RAM, and the scheduler takes the depths of the list before the one the
// RDP is done with, whose copy landed while that one was drawn: the pending index starts a list behind. A frame draws
// the glares of the list three before it instead of two, and the scheduler has taken that list's depths by then, as
// the game's own lists have theirs: a frame starts only once the list two before it is done (the main loop starts no
// tick with two graphics tasks out). Where the copy is in place (the desktop's) the game keeps its own three lists.
//
// g_SchedArtifactsWithDualBuffers, whether a list's samples were taken, is 3 bytes followed by a word,
// g_SchedWriteArtifactsIndex (0x8008FA68 and 0x8008FA6C, syms/data_dump.toml), so its fourth byte is padding, which
// zbuf_save_artifact_depths and sched_update_pending_artifacts use for the fourth list as they are.
//
// A list's flag is set where its samples are taken, with the gun drawn, which also turns each of its glares' screen
// position into a pointer to its sample (struct artifact's union), and cleared when the scheduler has read the list. A
// level's reset finds the list the RDP finished last not read yet, since the pending index is a list behind, and the
// flag it leaves set says a list written again in the new level without the gun (G5 Building's opening cutscene, by its
// lights) has samples: the scheduler read its screen positions as pointers, outside RDRAM, and the page trapped. So the
// reset clears the flags too: they are the level before's.

#include "patches.h"
#include "bss.h"

extern struct artifact g_ArtifactLists[3][MAX_ARTIFACTS];
extern s32 g_SchedFrontArtifactsIndex;
extern s32 g_SchedPendingArtifactsIndex;
extern u16 g_ArtifactDepths0[384];
extern u16 g_ArtifactDepths1[384];
extern u16 g_ArtifactDepths2[384];

static struct artifact s_ArtifactList3[MAX_ARTIFACTS];
// Aligned as zbuf_get_artifacts_depth_samples rounds a buffer's start, so that all of it holds samples.
static u16 s_ArtifactDepths3[384] __attribute__((aligned(64)));

static s32 artifacts_num_lists(void)
{
	return recomp_pd_render_to_ram_late() ? 4 : 3;
}

static struct artifact *artifacts_list(s32 index)
{
	return index == 3 ? s_ArtifactList3 : g_ArtifactLists[index];
}

RECOMP_PATCH void sched_init_artifacts(void)
{
	u8 *withdualbuffers = g_SchedArtifactsWithDualBuffers;
	s32 i;
	s32 j;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < MAX_ARTIFACTS; j++) {
			artifacts_list(i)[j].type = ARTIFACTTYPE_FREE;
		}

		withdualbuffers[i] = false;
	}
}

RECOMP_PATCH struct artifact *sched_get_write_artifacts(void)
{
	return artifacts_list(g_SchedWriteArtifactsIndex);
}

RECOMP_PATCH struct artifact *sched_get_front_artifacts(void)
{
	return artifacts_list(g_SchedFrontArtifactsIndex);
}

RECOMP_PATCH struct artifact *sched_get_pending_artifacts(void)
{
	return artifacts_list(g_SchedPendingArtifactsIndex);
}

RECOMP_PATCH void sched_increment_write_artifacts(void)
{
	g_SchedWriteArtifactsIndex = (g_SchedWriteArtifactsIndex + 1) % artifacts_num_lists();
}

RECOMP_PATCH void sched_increment_front_artifacts(void)
{
	g_SchedFrontArtifactsIndex = (g_SchedFrontArtifactsIndex + 1) % artifacts_num_lists();
}

RECOMP_PATCH void sched_increment_pending_artifacts(void)
{
	g_SchedPendingArtifactsIndex = (g_SchedPendingArtifactsIndex + 1) % artifacts_num_lists();
}

RECOMP_PATCH void sched_reset_artifacts(void)
{
	g_SchedWriteArtifactsIndex = 0;
	g_SchedFrontArtifactsIndex = 1;
	g_SchedPendingArtifactsIndex = 0;

	if (recomp_pd_render_to_ram_late()) {
		u8 *withdualbuffers = g_SchedArtifactsWithDualBuffers;
		s32 i;

		g_SchedPendingArtifactsIndex = 3;

		for (i = 0; i < 4; i++) {
			withdualbuffers[i] = false;
		}
	}
}

RECOMP_PATCH u16 *zbuf_get_artifacts_depth_samples(s32 index)
{
	u16 *addr = g_ArtifactDepths0;

	if (index == 1) {
		addr = g_ArtifactDepths1;
	} else if (index == 2) {
		addr = g_ArtifactDepths2;
	} else if (index == 3) {
		addr = s_ArtifactDepths3;
	}

	addr = (u16 *) (((uintptr_t) addr + 0x3f) & ~0x3f);

	return addr;
}
