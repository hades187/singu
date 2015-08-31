/**
 * @file llfloateroutbox.cpp
 * @brief Implementation of the merchant outbox window
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloateroutbox.h"

#include "llfolderview.h"
#include "llinventorybridge.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llinventoryobserver.h"
#include "llinventorypanel.h"
#include "llmarketplacefunctions.h"
#include "llnotifications.h"
#include "llnotificationsutil.h"
#include "lltextbox.h"
#include "lltexteditor.h"
//#include "lltransientfloatermgr.h"
#include "lltrans.h"
#include "llviewernetwork.h"
//#include "llwindowshade.h"

#define USE_WINDOWSHADE_DIALOGS	0


///----------------------------------------------------------------------------
/// LLOutboxNotification class
///----------------------------------------------------------------------------

class LLOutboxNotification
{
public:
	bool processNotification(const LLSD& notify)
	{
		LLFloaterOutbox* outbox_floater = LLFloaterOutbox::getInstance();

		outbox_floater->showNotification(notify);

		return false;
	}
};

///----------------------------------------------------------------------------
/// LLOutboxAddedObserver helper class
///----------------------------------------------------------------------------

class LLOutboxAddedObserver : public LLInventoryCategoryAddedObserver
{
public:
	LLOutboxAddedObserver(LLFloaterOutbox * outboxFloater)
		: LLInventoryCategoryAddedObserver()
		, mOutboxFloater(outboxFloater)
	{
	}

	void done()
	{
		for (cat_vec_t::iterator it = mAddedCategories.begin(); it != mAddedCategories.end(); ++it)
		{
			LLViewerInventoryCategory* added_category = *it;

			LLFolderType::EType added_category_type = added_category->getPreferredType();

			if (added_category_type == LLFolderType::FT_OUTBOX)
			{
				mOutboxFloater->initializeMarketPlace();
			}
		}
	}

private:
	LLFloaterOutbox *	mOutboxFloater;
};

///----------------------------------------------------------------------------
/// LLFloaterOutbox
///----------------------------------------------------------------------------

LLFloaterOutbox::LLFloaterOutbox(const LLSD& key)
	: LLFloater(key)
	, mCategoriesObserver(NULL)
	, mCategoryAddedObserver(NULL)
	, mImportBusy(false)
	, mImportButton(NULL)
	, mInventoryFolderCountText(NULL)
	, mInventoryImportInProgress(NULL)
	, mInventoryPlaceholder(NULL)
	, mInventoryText(NULL)
	, mInventoryTitle(NULL)
	, mOutboxId(LLUUID::null)
	, mOutboxItemCount(0)
	, mOutboxTopLevelDropZone(NULL)
//	, mWindowShade(NULL)
{
	LLUICtrlFactory::getInstance()->buildFloater(this, "floater_merchant_outbox.xml");

}

LLFloaterOutbox::~LLFloaterOutbox()
{
	if (mCategoriesObserver && gInventory.containsObserver(mCategoriesObserver))
	{
		gInventory.removeObserver(mCategoriesObserver);
	}
	delete mCategoriesObserver;

	if (mCategoryAddedObserver && gInventory.containsObserver(mCategoryAddedObserver))
	{
		gInventory.removeObserver(mCategoryAddedObserver);
	}
	delete mCategoryAddedObserver;
}

BOOL LLFloaterOutbox::postBuild()
{
	mInventoryFolderCountText = getChild<LLTextBox>("outbox_folder_count");
	mInventoryImportInProgress = getChild<LLView>("import_progress_indicator");
	mInventoryPlaceholder = getChild<LLView>("outbox_inventory_placeholder_panel");
	mInventoryText = mInventoryPlaceholder->getChild<LLTextEditor>("outbox_inventory_placeholder_text");
	// For some reason, adding style doesn't work unless this is true.
	mInventoryText->setParseHTML(TRUE);

	mInventoryTitle = mInventoryPlaceholder->getChild<LLTextBox>("outbox_inventory_placeholder_title");

	mImportButton = getChild<LLButton>("outbox_import_btn");
	mImportButton->setCommitCallback(boost::bind(&LLFloaterOutbox::onImportButtonClicked, this));

	// Set up the outbox inventory view
	LLInventoryPanel* inventory_panel = getChild<LLInventoryPanel>("panel_outbox_inventory");
	mOutboxInventoryPanel = inventory_panel->getInventoryPanelHandle();
	llassert(mOutboxInventoryPanel.get() != NULL);

	mOutboxTopLevelDropZone = getChild<LLPanel>("outbox_generic_drag_target");

	LLFocusableElement::setFocusReceivedCallback(boost::bind(&LLFloaterOutbox::onFocusReceived, this));

	// Observe category creation to catch outbox creation (moot if already existing)
	mCategoryAddedObserver = new LLOutboxAddedObserver(this);
	gInventory.addObserver(mCategoryAddedObserver);

	return TRUE;
}

void LLFloaterOutbox::cleanOutbox()
{
	// Note: we cannot delete the mOutboxInventoryPanel as that point
	// as this is called through callback observers of the panel itself.
	// Doing so would crash rapidly.

	// Invalidate the outbox data
	mOutboxId.setNull();
	mOutboxItemCount = 0;
}

void LLFloaterOutbox::onClose(bool app_quitting)
{
/*
	if (mWindowShade)
	{
		delete mWindowShade;

		mWindowShade = NULL;
	}
*/
	setVisible(FALSE);
	//destroy();	//Don't because outbox should be persistent.
}

void LLFloaterOutbox::onOpen()
{
	//
	// Initialize the Market Place or go update the outbox
	//

	if (LLMarketplaceInventoryImporter::getInstance()->getMarketPlaceStatus() == MarketplaceStatusCodes::MARKET_PLACE_NOT_INITIALIZED)
	{
		initializeMarketPlace();
	}
	else
	{
		setupOutbox();
	}

	//
	// Update the floater view
	//
	updateView();

	//
	// Trigger fetch of outbox contents
	//
	fetchOutboxContents();
}

void LLFloaterOutbox::onFocusReceived()
{
	fetchOutboxContents();
}

void LLFloaterOutbox::fetchOutboxContents()
{
	if (mOutboxId.notNull())
	{
		LLInventoryModelBackgroundFetch::instance().start(mOutboxId);
	}
}

void LLFloaterOutbox::setupOutbox()
{
	if (LLMarketplaceInventoryImporter::getInstance()->getMarketPlaceStatus() != MarketplaceStatusCodes::MARKET_PLACE_MERCHANT)
	{
		// If we are *not* a merchant or we have no market place connection established yet, do nothing
		return;
	}

	// We are a merchant. Get the outbox, create it if needs be.
	LLUUID outbox_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_OUTBOX, true);
	if (outbox_id.isNull())
	{
		// We should never get there unless inventory fails badly
		llerrs << "Inventory problem: failure to create the outbox for a merchant!" << llendl;
		return;
	}

	// Consolidate Merchant Outbox
	// We shouldn't have to do that but with a client/server system relying on a "well known folder" convention, things get messy and conventions get broken down eventually
	gInventory.consolidateForType(outbox_id, LLFolderType::FT_OUTBOX);

	if (outbox_id == mOutboxId)
	{

		llwarns << "Inventory warning: Merchant outbox already set" << llendl;
		return;
	}
	mOutboxId = outbox_id;

	// No longer need to observe new category creation
	if (mCategoryAddedObserver && gInventory.containsObserver(mCategoryAddedObserver))
	{
		gInventory.removeObserver(mCategoryAddedObserver);
		delete mCategoryAddedObserver;
		mCategoryAddedObserver = NULL;
	}
	llassert(!mCategoryAddedObserver);

	// Create observer for outbox modifications : clear the old one and create a new one
	if (mCategoriesObserver && gInventory.containsObserver(mCategoriesObserver))
	{
		gInventory.removeObserver(mCategoriesObserver);
		delete mCategoriesObserver;
	}
	mCategoriesObserver = new LLInventoryCategoriesObserver();
	gInventory.addObserver(mCategoriesObserver);
	mCategoriesObserver->addCategory(mOutboxId, boost::bind(&LLFloaterOutbox::onOutboxChanged, this));
	llassert(mCategoriesObserver);

	// Set up the outbox inventory view
	// Singu Note: we handle this in postBuild, grabbing the panel from the built xml.
	LLInventoryPanel* inventory_panel = mOutboxInventoryPanel.get();

	// Reshape the inventory to the proper size
	LLRect inventory_placeholder_rect = mInventoryPlaceholder->getRect();
	inventory_panel->setShape(inventory_placeholder_rect);

	// Set the sort order newest to oldest
	inventory_panel->setSortOrder(LLInventoryFilter::SO_FOLDERS_BY_NAME);
	inventory_panel->getFilter()->markDefault();

	// Get the content of the outbox
	fetchOutboxContents();
}

void LLFloaterOutbox::initializeMarketPlace()
{
	//
	// Initialize the marketplace import API
	//
	LLMarketplaceInventoryImporter& importer = LLMarketplaceInventoryImporter::instance();

	if (!importer.isInitialized())
	{
		importer.setInitializationErrorCallback(boost::bind(&LLFloaterOutbox::initializationReportError, this, _1, _2));
		importer.setStatusChangedCallback(boost::bind(&LLFloaterOutbox::importStatusChanged, this, _1));
		importer.setStatusReportCallback(boost::bind(&LLFloaterOutbox::importReportResults, this, _1, _2));
		importer.initialize();
	}
}

void LLFloaterOutbox::setStatusString(const std::string& statusString)
{
	llassert(mInventoryFolderCountText != NULL);

	mInventoryFolderCountText->setText(statusString);
}

void LLFloaterOutbox::updateFolderCount()
{
	if (mOutboxInventoryPanel.get() && mOutboxId.notNull())
	{
		S32 item_count = 0;

		if (mOutboxId.notNull())
		{
			LLInventoryModel::cat_array_t * cats;
			LLInventoryModel::item_array_t * items;
			gInventory.getDirectDescendentsOf(mOutboxId, cats, items);

			item_count = cats->count() + items->count();
		}

		mOutboxItemCount = item_count;
	}
	else
	{
		// If there's no outbox, the number of items in it should be set to 0 for consistency
		mOutboxItemCount = 0;
	}

	if (!mImportBusy)
	{
		updateFolderCountStatus();
	}
}

void LLFloaterOutbox::updateFolderCountStatus()
{
	if (mOutboxInventoryPanel.get() && mOutboxId.notNull())
	{
		switch (mOutboxItemCount)
		{
			case 0:	setStatusString(getString("OutboxFolderCount0"));	break;
			case 1:	setStatusString(getString("OutboxFolderCount1"));	break;
			default:
			{
				std::string item_count_str = llformat("%d", mOutboxItemCount);

				LLStringUtil::format_map_t args;
				args["[NUM]"] = item_count_str;

				setStatusString(getString("OutboxFolderCountN", args));
				break;
			}
		}
	}

	mImportButton->setEnabled(mOutboxItemCount > 0);
}

void LLFloaterOutbox::updateView()
{
	//updateView() is called twice the first time.
	updateFolderCount();
	LLInventoryPanel* panel = mOutboxInventoryPanel.get();

	if (mOutboxItemCount > 0)
	{
		panel->setVisible(TRUE);
		mInventoryPlaceholder->setVisible(FALSE);
		mOutboxTopLevelDropZone->setVisible(TRUE);
	}
	else
	{
		if (panel)
		{
			panel->setVisible(FALSE);
		}

		// Show the drop zone if there is an outbox folder
		mOutboxTopLevelDropZone->setVisible(mOutboxId.notNull());

		mInventoryPlaceholder->setVisible(TRUE);

		std::string outbox_text;
		std::string outbox_text2;
		std::string outbox_title;
		std::string outbox_tooltip;

		const LLSD& subs = getMarketplaceStringSubstitutions();
		U32 mkt_status = LLMarketplaceInventoryImporter::getInstance()->getMarketPlaceStatus();

		// Text styles for marketplace hyperlinks
		std::string subs_link;
		std::string subs_text;

		if (mOutboxId.notNull())
		{
			// Does the outbox needs recreation?
			if ((mOutboxInventoryPanel.get() == NULL) || !gInventory.getCategory(mOutboxId))
			{
				setupOutbox();
			}
			// "Outbox is empty!" message strings
			outbox_text = LLTrans::getString("InventoryOutboxNoItems");
			subs_link = "[MARKETPLACE_DASHBOARD_URL]";
			subs_text = " " + LLTrans::getString("InventoryOutboxNoItemsSubs");
			outbox_text2 = LLTrans::getString("InventoryOutboxNoItems2");
			outbox_title = LLTrans::getString("InventoryOutboxNoItemsTitle");
			outbox_tooltip = LLTrans::getString("InventoryOutboxNoItemsTooltip");
		}
		else if (mkt_status <= MarketplaceStatusCodes::MARKET_PLACE_INITIALIZING)
		{
			// "Initializing!" message strings
			outbox_text = LLTrans::getString("InventoryOutboxInitializing");
			subs_link = "[MARKETPLACE_CREATE_STORE_URL]";
			subs_text = " " + LLTrans::getString("InventoryOutboxInitializingSubs");
			outbox_text2 = LLTrans::getString("InventoryOutboxInitializing2");
			outbox_title = LLTrans::getString("InventoryOutboxInitializingTitle");
			outbox_tooltip = LLTrans::getString("InventoryOutboxInitializingTooltip");
		}
		else if (mkt_status == MarketplaceStatusCodes::MARKET_PLACE_NOT_MERCHANT)
		{
			// "Not a merchant!" message strings
			outbox_text = LLTrans::getString("InventoryOutboxNotMerchant");
			subs_link = "[MARKETPLACE_CREATE_STORE_URL]";
			subs_text = " " + LLTrans::getString("InventoryOutboxNotMerchantSubs");
			outbox_text2 = LLTrans::getString("InventoryOutboxNotMerchant2");
			outbox_title = LLTrans::getString("InventoryOutboxNotMerchantTitle");
			outbox_tooltip = LLTrans::getString("InventoryOutboxNotMerchantTooltip");
		}
		else
		{
			// "Errors!" message strings
			outbox_text = LLTrans::getString("InventoryOutboxError");
			subs_link = "[MARKETPLACE_CREATE_STORE_URL]";
			subs_text = " " + LLTrans::getString("InventoryOutboxErrorSubs");
			outbox_text2 = " " + LLTrans::getString("InventoryOutboxError2");
			outbox_title = LLTrans::getString("InventoryOutboxErrorTitle");
			outbox_tooltip = LLTrans::getString("InventoryOutboxErrorTooltip");
		}

		mInventoryText->clear();
		const LLColor4 color = gColors.getColor("TextFgReadOnlyColor");
		mInventoryText->appendColoredText(outbox_text, false, false, color);
		LLStringUtil::format(subs_link, subs);
		LLStyleSP subs_link_style(new LLStyle);
		subs_link_style->setLinkHREF(subs_link);
		subs_link_style->setColor(gSavedSettings.getColor4("HTMLLinkColor"));
		mInventoryText->appendStyledText(subs_text, false, false, subs_link_style);
		mInventoryText->appendColoredText(outbox_text2, false, false, color);
		mInventoryTitle->setValue(outbox_title);
		mInventoryPlaceholder->getParent()->setToolTip(outbox_tooltip);
	}
}

bool isAccepted(EAcceptance accept)
{
	return (accept >= ACCEPT_YES_COPY_SINGLE);
}

BOOL LLFloaterOutbox::handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop,
										EDragAndDropType cargo_type,
										void* cargo_data,
										EAcceptance* accept,
										std::string& tooltip_msg)
{
	if ((mOutboxInventoryPanel.get() == NULL) ||
		//(mWindowShade && mWindowShade->isShown()) ||
		LLMarketplaceInventoryImporter::getInstance()->isImportInProgress() ||
		mOutboxId.isNull())
	{
		return FALSE;
	}

	LLView * handled_view = childrenHandleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data, accept, tooltip_msg);
	BOOL handled = (handled_view != NULL);

	// Determine if the mouse is inside the inventory panel itself or just within the floater
	bool pointInInventoryPanel = false;
	bool pointInInventoryPanelChild = false;
	LLInventoryPanel* panel = mOutboxInventoryPanel.get();
	LLFolderView * root_folder = panel->getRootFolder();
	if (panel->getVisible())
	{
		S32 inv_x, inv_y;
		localPointToOtherView(x, y, &inv_x, &inv_y, panel);

		pointInInventoryPanel = panel->getRect().pointInRect(inv_x, inv_y);

		LLView * inventory_panel_child_at_point = panel->childFromPoint(inv_x, inv_y, true);
		pointInInventoryPanelChild = (inventory_panel_child_at_point != root_folder);
	}

	// Pass all drag and drop for this floater to the outbox inventory control
	if (!handled || !isAccepted(*accept))
	{
		// Handle the drag and drop directly to the root of the outbox if we're not in the inventory panel
		// (otherwise the inventory panel itself will handle the drag and drop operation, without any override)
		if (!pointInInventoryPanel)
		{
			handled = root_folder->handleDragAndDropToThisFolder(mask, drop, cargo_type, cargo_data, accept, tooltip_msg);
		}

		mOutboxTopLevelDropZone->setBackgroundVisible(handled && !drop && isAccepted(*accept));
	}
	else
	{
		mOutboxTopLevelDropZone->setBackgroundVisible(!pointInInventoryPanelChild);
	}

	return handled;
}

BOOL LLFloaterOutbox::handleHover(S32 x, S32 y, MASK mask)
{
	mOutboxTopLevelDropZone->setBackgroundVisible(FALSE);

	return LLFloater::handleHover(x, y, mask);
}
/*
void LLFloaterOutbox::onMouseLeave(S32 x, S32 y, MASK mask)
{
	mOutboxTopLevelDropZone->setBackgroundVisible(FALSE);

	LLFloater::onMouseLeave(x, y, mask);
}
*/
void LLFloaterOutbox::onImportButtonClicked()
{
	if (mOutboxInventoryPanel.get())
	{
		mOutboxInventoryPanel.get()->clearSelection();
	}

	mImportBusy = LLMarketplaceInventoryImporter::instance().triggerImport();
}

void LLFloaterOutbox::onOutboxChanged()
{
	LLViewerInventoryCategory* category = gInventory.getCategory(mOutboxId);
	if (mOutboxId.notNull() && category)
	{
		fetchOutboxContents();
		updateView();
	}
	else
	{
		cleanOutbox();
	}
}

void LLFloaterOutbox::importReportResults(U32 status, const LLSD& content)
{
	if (status == MarketplaceErrorCodes::IMPORT_DONE)
	{
		LLNotificationsUtil::add("OutboxImportComplete");
	}
	else if (status == MarketplaceErrorCodes::IMPORT_DONE_WITH_ERRORS)
	{
		const LLSD& subs = getMarketplaceStringSubstitutions();

		LLNotificationsUtil::add("OutboxImportHadErrors", subs);
	}
	else
	{
		char status_string[16];
		sprintf(status_string, "%d", status);

		LLSD subs;
		subs["[ERROR_CODE]"] = status_string;

		LLNotificationsUtil::add("OutboxImportFailed", subs);
	}

	updateView();
}

void LLFloaterOutbox::importStatusChanged(bool inProgress)
{
	if (mOutboxId.isNull() && (LLMarketplaceInventoryImporter::getInstance()->getMarketPlaceStatus() == MarketplaceStatusCodes::MARKET_PLACE_MERCHANT))
	{
		setupOutbox();
	}

	if (inProgress)
	{
		if (mImportBusy)
		{
			setStatusString(getString("OutboxImporting"));
		}
		else
		{
			setStatusString(getString("OutboxInitializing"));
		}

		mImportBusy = true;
		mImportButton->setEnabled(false);
		mInventoryImportInProgress->setVisible(true);
	}
	else
	{
		setStatusString("");
		mImportBusy = false;
		mImportButton->setEnabled(mOutboxItemCount > 0);
		mInventoryImportInProgress->setVisible(false);
	}

	updateView();
}

void LLFloaterOutbox::initializationReportError(U32 status, const LLSD& content)
{
	if (status >= MarketplaceErrorCodes::IMPORT_BAD_REQUEST)
	{
		char status_string[16];
		sprintf(status_string, "%d", status);

		LLSD subs;
		subs["[ERROR_CODE]"] = status_string;

		LLNotificationsUtil::add("OutboxInitFailed", subs);
	}

	updateView();
}

void LLFloaterOutbox::showNotification(const LLSD& notify)
{
	LLNotificationPtr notification = LLNotifications::instance().find(notify["id"].asUUID());

	if (!notification)
	{
		llerrs << "Unable to find outbox notification!" << notify.asString() << llendl;

		return;
	}

#if USE_WINDOWSHADE_DIALOGS

	if (mWindowShade)
	{
		delete mWindowShade;
	}

	LLRect floater_rect = getLocalRect();
	floater_rect.mTop -= getHeaderHeight();
	floater_rect.stretch(-5, 0);

	LLWindowShade::Params params;
	params.name = "notification_shade";
	params.rect = floater_rect;
	params.follows.flags = FOLLOWS_ALL;
	params.modal = true;
	params.can_close = false;
	params.shade_color = LLColor4::white % 0.25f;
	params.text_color = LLColor4::white;

	mWindowShade = LLUICtrlFactory::create<LLWindowShade>(params);

	addChild(mWindowShade);
	mWindowShade->show(notification);

#else //USE_WINDOWSHADE_DIALOGS
/*
	LLNotificationsUI::LLEventHandler * handler =
		LLNotificationsUI::LLNotificationManager::instance().getHandlerForNotification("alertmodal");

	LLNotificationsUI::LLSysHandler * sys_handler = dynamic_cast<LLNotificationsUI::LLSysHandler *>(handler);
	llassert(sys_handler);

	sys_handler->processNotification(notify);
*/
	LLNotifications::instance().add(notification);
#endif //USE_WINDOWSHADE_DIALOGS
}

