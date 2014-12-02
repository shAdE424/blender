/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/hair/edithair.c
 *  \ingroup bke
 */

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_mempool.h"

#include "DNA_customdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"

#include "BKE_bvhutils.h"
#include "BKE_customdata.h"
#include "BKE_edithair.h"
#include "BKE_DerivedMesh.h"
#include "BKE_particle.h"

#include "intern/bmesh_strands_conv.h"

BMEditStrands *BKE_editstrands_create(BMesh *bm)
{
	BMEditStrands *es = MEM_callocN(sizeof(BMEditStrands), __func__);
	
	es->bm = bm;
	
	return es;
}

BMEditStrands *BKE_editstrands_copy(BMEditStrands *es)
{
	BMEditStrands *es_copy = MEM_callocN(sizeof(BMEditStrands), __func__);
	*es_copy = *es;
	
	es_copy->bm = BM_mesh_copy(es->bm);
	
	return es_copy;
}

/**
 * \brief Return the BMEditStrands for a given object
 */
BMEditStrands *BKE_editstrands_from_object(Object *ob)
{
	ParticleSystem *psys = psys_get_current(ob);
	if (psys) {
		return psys->hairedit;
	}
	return NULL;
}

void BKE_editstrands_update_linked_customdata(BMEditStrands *es)
{
	BMesh *bm = es->bm;
	
	/* this is done for BMEditMesh, but should never exist for strands */
	BLI_assert(!CustomData_has_layer(&bm->pdata, CD_MTEXPOLY));
}

/*does not free the BMEditStrands struct itself*/
void BKE_editstrands_free(BMEditStrands *es)
{
	if (es->bm)
		BM_mesh_free(es->bm);
}

/* === constraints === */

void BKE_editstrands_calc_segment_lengths(BMesh *bm)
{
	BMVert *root, *v, *vprev;
	BMIter iter, iter_strand;
	int k;
	
	BM_ITER_STRANDS(root, &iter, bm, BM_STRANDS_OF_MESH) {
		BM_ITER_STRANDS_ELEM_INDEX(v, &iter_strand, root, BM_VERTS_OF_STRAND, k) {
			if (k > 0) {
				float length = len_v3v3(v->co, vprev->co);
				BM_elem_float_data_named_set(&bm->vdata, v, CD_PROP_FLT, CD_HAIR_SEGMENT_LENGTH, length);
			}
			vprev = v;
		}
	}
}

void BKE_editstrands_solve_constraints(BMEditStrands *es)
{
	/* XXX Simplistic implementation from particles:
	 * adjust segment lengths starting from the root.
	 * This should be replaced by a more advanced method using a least-squares
	 * error metric with length and root location constraints
	 */
	
	BMesh *bm = es->bm;
	BMVert *root, *v, *vprev;
	BMIter iter, iter_strand;
	int k;
	
	BM_ITER_STRANDS(root, &iter, bm, BM_STRANDS_OF_MESH) {
		BM_ITER_STRANDS_ELEM_INDEX(v, &iter_strand, root, BM_VERTS_OF_STRAND, k) {
			if (k > 0) {
				float base_length = BM_elem_float_data_named_get(&bm->vdata, v, CD_PROP_FLT, CD_HAIR_SEGMENT_LENGTH);
				float dist[3];
				float length;
				
				sub_v3_v3v3(dist, v->co, vprev->co);
				length = len_v3(dist);
				if (length > 0.0f)
					madd_v3_v3v3fl(v->co, vprev->co, dist, base_length / length);
			}
			vprev = v;
		}
	}
}


/* === particle conversion === */

BMesh *BKE_particles_to_bmesh(Object *ob, ParticleSystem *psys)
{
	ParticleSystemModifierData *psmd = psys_get_modifier(ob, psys);
	
	const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_PSYS(psys);
	BMesh *bm;

	bm = BM_mesh_create(&allocsize);

	if (psmd && psmd->dm) {
		DM_ensure_tessface(psmd->dm);
		
		BM_strands_bm_from_psys(bm, ob, psys, psmd->dm, true, psys->shapenr);
		
		BKE_editstrands_calc_segment_lengths(bm);
	}

	return bm;
}

void BKE_particles_from_bmesh(Object *ob, ParticleSystem *psys)
{
	ParticleSystemModifierData *psmd = psys_get_modifier(ob, psys);
	BMesh *bm = psys->hairedit ? psys->hairedit->bm : NULL;
	
	if (bm) {
		if (psmd && psmd->dm) {
			BVHTreeFromMesh bvhtree = {NULL};
			
			DM_ensure_tessface(psmd->dm);
			
			bvhtree_from_mesh_faces(&bvhtree, psmd->dm, 0.0, 2, 6);
			
			BM_strands_bm_to_psys(bm, ob, psys, psmd->dm, &bvhtree);
			
			free_bvhtree_from_mesh(&bvhtree);
		}
	}
}
