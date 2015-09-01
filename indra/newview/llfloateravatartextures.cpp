/** 
 * @file llfloateravatartextures.cpp
 * @brief Debugging view showing underlying avatar textures and baked textures.
 *
 * $LicenseInfo:firstyear=2006&license=viewergpl$
 * 
 * Copyright (c) 2006-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloateravatartextures.h"

#include "lltexturectrl.h"

#include "lluictrlfactory.h"
#include "llviewerobjectlist.h"
#include "llvoavatar.h"
#include "llpreviewtexture.h"
#include "llviewercontrol.h"

using namespace LLAvatarAppearanceDefines;

LLFloaterAvatarTextures::LLFloaterAvatarTextures(const LLUUID& id) : 
	LLFloater(std::string("avatar_texture_debug")),
	mID(id)
{
}

LLFloaterAvatarTextures::~LLFloaterAvatarTextures()
{
}

LLFloaterAvatarTextures* LLFloaterAvatarTextures::show(const LLUUID &id)
{

	LLFloaterAvatarTextures* floaterp = new LLFloaterAvatarTextures(id);

	// Builds and adds to gFloaterView
	LLUICtrlFactory::getInstance()->buildFloater(floaterp, "floater_avatar_textures.xml");

	gFloaterView->addChild(floaterp);
	floaterp->open();	/*Flawfinder: ignore*/

	gFloaterView->adjustToFitScreen(floaterp, FALSE);

	return floaterp;
}

BOOL LLFloaterAvatarTextures::postBuild()
{
	for (U32 i=0; i < TEX_NUM_INDICES; i++)
	{
		const std::string tex_name = LLAvatarAppearanceDictionary::getInstance()->getTexture(ETextureIndex(i))->mName;
		mTextures[i] = getChild<LLTextureCtrl>(tex_name);
	}
	mTitle = getTitle();

//	childSetAction("Dump", onClickDump, this);
    childSetAction("Open", onClickOpen, &mID);

	refresh();
	return TRUE;
}

void LLFloaterAvatarTextures::draw()
{
	refresh();
	LLFloater::draw();
}

// <edit>
//#if !LL_RELEASE_FOR_DOWNLOAD
// </edit>
static void update_texture_ctrl(LLVOAvatar* avatarp,
								 LLTextureCtrl* ctrl,
								 ETextureIndex te)
{
	LLUUID id = avatarp->getTE(te)->getID();
	if (id == IMG_DEFAULT_AVATAR)
	{
		ctrl->setImageAssetID(LLUUID::null);
		ctrl->setToolTip(std::string("IMG_DEFAULT_AVATAR"));
	}
	else
	{
		ctrl->setImageAssetID(id);
		ctrl->setToolTip(id.asString());
	}
}

static LLVOAvatar* find_avatar(const LLUUID& id)
{
	LLViewerObject *obj = gObjectList.findObject(id);
	while (obj && obj->isAttachment())
	{
		obj = (LLViewerObject *)obj->getParent();
	}

	if (obj && obj->isAvatar())
	{
		return (LLVOAvatar*)obj;
	}
	else
	{
		return NULL;
	}
}

void LLFloaterAvatarTextures::refresh()
{
	LLVOAvatar *avatarp = find_avatar(mID);
	if (avatarp)
	{
		std::string fullname;
		if (gCacheName->getFullName(avatarp->getID(), fullname))
		{
			setTitle(mTitle + ": " + fullname);
		}
		for (U32 i=0; i < TEX_NUM_INDICES; i++)
		{
			update_texture_ctrl(avatarp, mTextures[i], ETextureIndex(i));
		}
	}
	else
	{
		setTitle(mTitle + ": INVALID AVATAR (" + mID.asString() + ")");
	}
}

// <edit>
/*
// </edit>
#else

void LLFloaterAvatarTextures::refresh()
{
}

#endif
// <edit>
*/
// </edit>

void LLFloaterAvatarTextures::onClickOpen(void* userdata)
{
    LLUUID* avID = (LLUUID*)userdata;

	LLVOAvatar *avatarp = find_avatar(*avID);

	for (U32 i=0; i < TEX_NUM_INDICES; i++)
	{
    	LLUUID image_id = avatarp->getTE(ETextureIndex(i))->getID();

        if (image_id != IMG_DEFAULT_AVATAR)
        {
            // See if we can bring an existing preview to the front
            if (!LLPreview::show(image_id))
	        {
		        // There isn't one, so make a new preview
		        S32 left, top;
		        gFloaterView->getNewFloaterPosition(&left, &top);
		        LLRect rect = gSavedSettings.getRect("PreviewTextureRect");
		        rect.translate( left - rect.mLeft, top - rect.mTop );

		        LLPreviewTexture* preview;
		        preview = new LLPreviewTexture(image_id.asString(),
                                               rect,
                                               image_id.asString(),
                                               image_id,
                                               FALSE);
		        preview->setSourceID(LLUUID::null);
		        preview->setFocus(TRUE);

		        gFloaterView->adjustToFitScreen(preview, FALSE);
	        }
        }
    }
}
