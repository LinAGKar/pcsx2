/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021 PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "GS/Renderers/HW/GSRendererHW.h"
#include "GS/Renderers/HW/GSVertexHW.h"

class GSRendererNew final : public GSRendererHW
{
private:
	GSHWDrawConfig m_conf;

private:
	inline void ResetStates();
	inline void SetupIA(const float& sx, const float& sy);
	inline void EmulateTextureShuffleAndFbmask();
	inline void EmulateChannelShuffle(const GSTextureCache::Source* tex);
	inline void EmulateBlending(bool& DATE_PRIMID, bool& DATE_BARRIER);
	inline void EmulateTextureSampler(const GSTextureCache::Source* tex);
	inline void EmulateZbuffer();
	inline void EmulateATST(float& AREF, GSHWDrawConfig::PSSelector& ps, bool pass_2);

public:
	GSRendererNew();
	~GSRendererNew() override {}

	void DrawPrims(GSTexture* rt, GSTexture* ds, GSTextureCache::Source* tex) override;

	bool IsDummyTexture() const override;
};
