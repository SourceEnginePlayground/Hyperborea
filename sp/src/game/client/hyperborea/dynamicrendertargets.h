/*
Hyperborea (c) by Nicolas @ https://github.com/NicolasDe

Hyperborea is licensed under a
Creative Commons Attribution-ShareAlike 4.0 International License.

You should have received a copy of the license along with this
work.  If not, see <http://creativecommons.org/licenses/by-sa/4.0/>.
*/
#pragma once

#include "game/client/iclientrendertargets.h"
#include "materialsystem/imaterialsystem.h"
#include "baseclientrendertargets.h"

class IMaterialSystem;
class IMaterialSystemHardwareConfig;

class DynamicRenderTargets : public CBaseClientRenderTargets, public CAutoGameSystemPerFrame
{
	DECLARE_CLASS_GAMEROOT(DynamicRenderTargets, CBaseClientRenderTargets);
public:
	virtual void InitClientRenderTargets(IMaterialSystem* MaterialSystem, IMaterialSystemHardwareConfig* MaterialSystemHardwareConfig);
	virtual void InitDynamicRenderTargets();

	virtual void ShutdownClientRenderTargets();
	virtual void ShutdownDynamicRenderTargets();

	virtual void PreRender();
	virtual void UpdateDynamicRenderTargets();

protected:
	virtual Vector2D GetViewport();

#if defined(GAMEUI2)
	virtual ITexture* CreateMaskGameUITexture();
#endif // GAMEUI2
	virtual ITexture* CreateDepthBufferTexture();
	virtual ITexture* CreatePhysgunBeamTexture();
	virtual ITexture* CreateMaskViewmodelTexture();

private:
	Vector2D OldViewport;

#if defined(GAMEUI2)
	CTextureReference MaskGameUITexture;
#endif // GAMEUI2
	CTextureReference DepthBufferTexture;
	CTextureReference PhysgunBeamTexture;
	CTextureReference MaskViewmodelTexture;

	IMaterialSystem* CurrentMaterialSystem;
};