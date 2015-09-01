/* Copyright (c) 2009
 *
 * Modular Systems All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *   3. Neither the name Modular Systems nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY MODULAR SYSTEMS AND CONTRIBUTORS AS IS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MODULAR SYSTEMS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "llviewerprecompiledheaders.h"

#include "chatbar_as_cmdline.h"

#include "llavatarnamecache.h"
#include "llcalc.h"

#include "llchatbar.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llagentui.h"
#include "llavataractions.h"
#include "llnotificationsutil.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "lleventtimer.h"

#include "llvolume.h"
#include "llvolumemessage.h"
#include "llurldispatcher.h"
#include "llworld.h"
#include "llworldmap.h"
#include "llfloateravatarlist.h"
#include "llfloaterregioninfo.h"
#include "llviewerobjectlist.h"
#include "llviewertexteditor.h"
#include "llviewermenu.h"
#include "llvoavatarself.h"
#include "lltooldraganddrop.h"
#include "llinventorymodel.h"
#include "llregioninfomodel.h"
#include "llselectmgr.h"
#include "llslurl.h"
#include "llurlaction.h"

#include "llchat.h"

#include "llfloaterchat.h"
#include "rlvhandler.h"
#include "llpreview.h"
#include "llpreviewtexture.h"

void cmdline_printchat(const std::string& message);
void cmdline_tp2name(std::string target);

LLUUID cmdline_partial_name2key(std::string name);



LLViewerInventoryItem::item_array_t findInventoryInFolder(const std::string& ifolder)
{
	LLUUID folder = gInventory.findCategoryByName(ifolder);
	LLViewerInventoryCategory::cat_array_t cats;
	LLViewerInventoryItem::item_array_t items;
	//ObjectContentNameMatches objectnamematches(ifolder);
	gInventory.collectDescendents(folder,cats,items,FALSE);//,objectnamematches);

	return items;
}

class JCZface : public LLEventTimer
{
public:
	JCZface(std::stack<LLViewerInventoryItem*> stack, LLUUID dest, F32 pause) : LLEventTimer( pause )
	{
		cmdline_printchat("initialized");
		instack = stack;
		indest = dest;
	}
	~JCZface()
	{
		cmdline_printchat("deinitialized");
	}
	BOOL tick()
	{
		LLViewerInventoryItem* subj = instack.top();
		instack.pop();
		LLViewerObject *objectp = gObjectList.findObject(indest);
		if (objectp)
		{
			cmdline_printchat(std::string("dropping ")+subj->getName());
			LLToolDragAndDrop::dropInventory(objectp,subj,LLToolDragAndDrop::SOURCE_AGENT,gAgent.getID());
			return (instack.size() == 0);
		}else
		{
			cmdline_printchat("object lost");
			return TRUE;
		}	
	}


private:
	std::stack<LLViewerInventoryItem*> instack;
	LLUUID indest;
};

class JCZtake : public LLEventTimer
{
public:
	static BOOL ztakeon;

	JCZtake() : LLEventTimer(2.0f)
	{
		ztakeon = FALSE;
		cmdline_printchat("initialized");
	}
	~JCZtake()
	{
		cmdline_printchat("deinitialized");
	}
	BOOL tick()
	{
		{
			LLMessageSystem *msg = gMessageSystem;
			for(LLObjectSelection::iterator itr=LLSelectMgr::getInstance()->getSelection()->begin();
				itr!=LLSelectMgr::getInstance()->getSelection()->end();++itr)
			{
				LLSelectNode* node = (*itr);
				LLViewerObject* object = node->getObject();
				U32 localid=object->getLocalID();
				if (done_prims.find(localid) == done_prims.end())
				{
					done_prims.insert(localid);
					std::string name = llformat("%fx%fx%f",object->getScale().mV[VX],object->getScale().mV[VY],object->getScale().mV[VZ]);
					cmdline_printchat(std::string("Rename&take ")+name);
					msg->newMessageFast(_PREHASH_ObjectName);
					msg->nextBlockFast(_PREHASH_AgentData);
					msg->addUUIDFast(_PREHASH_AgentID,gAgent.getID());
					msg->addUUIDFast(_PREHASH_SessionID,gAgent.getSessionID());
					msg->nextBlockFast(_PREHASH_ObjectData);
					msg->addU32Fast(_PREHASH_LocalID,localid);
					msg->addStringFast(_PREHASH_Name,name);
					gAgent.sendReliableMessage();

					msg->newMessageFast(_PREHASH_DeRezObject);
					msg->nextBlockFast(_PREHASH_AgentData);
					msg->addUUIDFast(_PREHASH_AgentID,gAgent.getID());
					msg->addUUIDFast(_PREHASH_SessionID,gAgent.getSessionID());
					msg->nextBlockFast(_PREHASH_AgentBlock);
					msg->addUUIDFast(_PREHASH_GroupID,LLUUID::null);
					msg->addU8Fast(_PREHASH_Destination,4);
					msg->addUUIDFast(_PREHASH_DestinationID,LLUUID::null);
					LLUUID rand;
					rand.generate();
					msg->addUUIDFast(_PREHASH_TransactionID,rand);
					msg->addU8Fast(_PREHASH_PacketCount,1);
					msg->addU8Fast(_PREHASH_PacketNumber,0);
					msg->nextBlockFast(_PREHASH_ObjectData);
					msg->addU32Fast(_PREHASH_ObjectLocalID,localid);
					gAgent.sendReliableMessage();
				}
			}
		}
		return ztakeon;
	}


private:
	std::set<U32> done_prims;
	
};

void invrepair()
{

	LLViewerInventoryCategory::cat_array_t cats;
	LLViewerInventoryItem::item_array_t items;
	//ObjectContentNameMatches objectnamematches(ifolder);
	gInventory.collectDescendents(gInventory.getRootFolderID(),cats,items,FALSE);//,objectnamematches);
}

#ifdef PROF_CTRL_CALLS
bool sort_calls(const std::pair<std::string, U32>& left, const std::pair<std::string, U32>& right)
{
	return left.second > right.second;
}
//Structure to be passed into LLControlGroup::applyToAll.
// Doesn't actually modify control group, but rather uses the 'apply'
// vfn to add each variable in the group to a list. This essentially
// allows us to copy the private variable list without touching the controlgroup
// class.
struct ProfCtrlListAccum : public LLControlGroup::ApplyFunctor
{
	virtual void apply(const std::string& name, LLControlVariable* control)
	{
		mVariableList.push_back(std::make_pair(name,control->mLookupCount));
	}
	std::vector<std::pair<std::string, U32> > mVariableList;
};
#endif //PROF_CTRL_CALLS
void spew_key_to_name(const LLUUID& targetKey, const LLAvatarName& av_name)
{
	cmdline_printchat(llformat("%s: %s", targetKey.asString().c_str(), av_name.getNSName().c_str()));
}
bool cmd_line_chat(std::string data, EChatType type)
{
	static LLCachedControl<bool> enableChatCmd(gSavedSettings, "AscentCmdLine", true);
	if (enableChatCmd)
	{
		data = utf8str_tolower(data);
		std::istringstream input(data);
		std::string cmd;

		if (!(input >> cmd))	return true;

		static LLCachedControl<std::string> sDrawDistanceCommand(gSavedSettings,  "AscentCmdLineDrawDistance");
		static LLCachedControl<std::string> sHeightCommand(gSavedSettings,  "AscentCmdLineHeight");
		static LLCachedControl<std::string> sGroundCommand(gSavedSettings,  "AscentCmdLineGround");
		static LLCachedControl<std::string> sPosCommand(gSavedSettings,  "AscentCmdLinePos");
		static LLCachedControl<std::string> sRezPlatCommand(gSavedSettings,  "AscentCmdLineRezPlatform");
		static LLCachedControl<std::string> sHomeCommand(gSavedSettings,  "AscentCmdLineTeleportHome");
		static LLCachedControl<std::string> sCalcCommand(gSavedSettings,  "AscentCmdLineCalc");
		static LLCachedControl<std::string> sMapToCommand(gSavedSettings,  "AscentCmdLineMapTo");
		static LLCachedControl<std::string> sClearCommand(gSavedSettings,  "AscentCmdLineClearChat");
		static LLCachedControl<std::string> sRegionMsgCommand(gSavedSettings,  "SinguCmdLineRegionSay");
		static LLCachedControl<std::string> sTeleportToCam(gSavedSettings,  "AscentCmdTeleportToCam");
		static LLCachedControl<std::string> sHoverHeight(gSavedSettings, "AlchemyChatCommandHoverHeight", "/hover");
		static LLCachedControl<std::string> sResyncAnimCommand(gSavedSettings, "AlchemyChatCommandResyncAnim", "/resync");
		static LLCachedControl<std::string> sKeyToName(gSavedSettings,  "AscentCmdLineKeyToName");
		static LLCachedControl<std::string> sOfferTp(gSavedSettings,  "AscentCmdLineOfferTp");
		static LLCachedControl<std::string> sTP2Command(gSavedSettings,  "AscentCmdLineTP2");
		static LLCachedControl<std::string> sAwayCommand(gSavedSettings,  "SinguCmdLineAway");
		static LLCachedControl<std::string> sURLCommand(gSavedSettings,  "SinguCmdLineURL");

		if (cmd == utf8str_tolower(sDrawDistanceCommand)) // dd
		{
			int drawDist;
			if (input >> drawDist)
			{
				gSavedSettings.setF32("RenderFarClip", drawDist);
				gAgentCamera.mDrawDistance=drawDist;
				char buffer[DB_IM_MSG_BUF_SIZE * 2];  /* Flawfinder: ignore */
				snprintf(buffer,sizeof(buffer),"Draw distance set to: %dm",drawDist);
				cmdline_printchat(std::string(buffer));
				return false;
			}
		}
		else if (cmd == utf8str_tolower(sHeightCommand)) // gth
		{
			F64 z;
			if (input >> z)
			{
				LLVector3d pos_global = gAgent.getPositionGlobal();
				pos_global.mdV[VZ] = z;
				gAgent.teleportViaLocationLookAt(pos_global);
				return false;
			}
		}
		else if (cmd == utf8str_tolower(sGroundCommand)) // flr
		{
			LLVector3d pos_global = gAgent.getPositionGlobal();
			pos_global.mdV[VZ] = 0.0;
			gAgent.teleportViaLocationLookAt(pos_global);
			return false;
		}
		else if (cmd == utf8str_tolower(sPosCommand)) // pos
		{
			F32 x,y,z;
			if ((input >> x) && (input >> y))
			{
				if (!(input >> z))
					z = gAgent.getPositionAgent().mV[VZ];

				if (LLViewerRegion* regionp = gAgent.getRegion())
				{
					LLVector3d target_pos = regionp->getPosGlobalFromRegion(LLVector3((F32) x, (F32) y, (F32) z));
					gAgent.teleportViaLocationLookAt(target_pos);
					return false;
				}
			}
		}
		else if (cmd == utf8str_tolower(sRezPlatCommand)) // plat
		{
			F32 size;
			static LLCachedControl<F32> platSize(gSavedSettings, "AscentPlatformSize");
			if (!(input >> size))
				size = static_cast<F32>(platSize);

			const LLVector3& agent_pos = gAgent.getPositionAgent();
			const LLVector3 rez_pos(agent_pos.mV[VX], agent_pos.mV[VY], agent_pos.mV[VZ] - ((gAgentAvatarp->getScale().mV[VZ] / 2.f) + 0.25f + (gAgent.getVelocity().magVec() * 0.333)));

			LLMessageSystem* msg = gMessageSystem;
			msg->newMessageFast(_PREHASH_ObjectAdd);
			msg->nextBlockFast(_PREHASH_AgentData);
			msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
			msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
			msg->addUUIDFast(_PREHASH_GroupID, gAgent.getGroupID());
			msg->nextBlockFast(_PREHASH_ObjectData);
			msg->addU8Fast(_PREHASH_PCode, LL_PCODE_VOLUME);
			msg->addU8Fast(_PREHASH_Material, LL_MCODE_METAL);
			msg->addU32Fast(_PREHASH_AddFlags, agent_pos.mV[VZ] > 4096.f ? FLAGS_CREATE_SELECTED : 0U);

			LLVolumeParams volume_params;
			volume_params.setType(LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_CIRCLE_33);
			volume_params.setRatio(2, 2);
			volume_params.setShear(0, 0);
			volume_params.setTaper(2.0f,2.0f);
			volume_params.setTaperX(0.f);
			volume_params.setTaperY(0.f);
			LLVolumeMessage::packVolumeParams(&volume_params, msg);

			msg->addVector3Fast(_PREHASH_Scale, LLVector3(size, size, 0.25f));
			msg->addQuatFast(_PREHASH_Rotation, LLQuaternion(90.f * DEG_TO_RAD, LLVector3::y_axis));
			msg->addVector3Fast(_PREHASH_RayStart, rez_pos);
			msg->addVector3Fast(_PREHASH_RayEnd, rez_pos);
			msg->addUUIDFast(_PREHASH_RayTargetID, LLUUID::null);
			msg->addU8Fast(_PREHASH_BypassRaycast, TRUE);
			msg->addU8Fast(_PREHASH_RayEndIsIntersection, FALSE);
			msg->addU8Fast(_PREHASH_State, FALSE);
			msg->sendReliable(gAgent.getRegionHost());

			return false;
		}
		else if (cmd == utf8str_tolower(sHomeCommand)) // home
		{
			gAgent.teleportHome();
			return false;
		}
		else if (cmd == utf8str_tolower(sCalcCommand))//Cryogenic Blitz
		{
			if (data.length() > cmd.length() + 1)
			{
				F32 result = 0.f;
				std::string expr = data.substr(cmd.length() + 1);
				LLStringUtil::toUpper(expr);
				if (LLCalc::getInstance()->evalString(expr, result))
				{
					// Replace the expression with the result
					LLSD args;
					args["EXPRESSION"] = expr;
					args["RESULT"] = result;
					LLNotificationsUtil::add("ChatCommandCalc", args);
					return false;
				}
				LLNotificationsUtil::add("ChatCommandCalcFailed");
				return false;
			}
		}
		else if (cmd == utf8str_tolower(sMapToCommand))
		{
			const std::string::size_type length = cmd.length() + 1;
			if (data.length() > length)
			{
				LLSLURL slurl(LLWeb::escapeURL(data.substr(length)));
				// The user wants to keep their position between MapTos and they have not passed a position (position is defaulted)
				static LLCachedControl<bool> sMapToKeepPos(gSavedSettings, "AscentMapToKeepPos");
				if (sMapToKeepPos && slurl.getPosition() == LLVector3(REGION_WIDTH_METERS/2, REGION_WIDTH_METERS/2, 0))
				{
					LLVector3d agentPos = gAgent.getPositionGlobal();
					slurl = LLSLURL(slurl.getRegion(), LLVector3(fmod(agentPos.mdV[VX], (F64)REGION_WIDTH_METERS), fmod(agentPos.mdV[VY], (F64)REGION_WIDTH_METERS), agentPos.mdV[VZ]));
				}

				LLUrlAction::teleportToLocation(LLWeb::escapeURL(std::string("secondlife:///app/teleport/")+slurl.getLocationString()));
			}
			return false;
		}
		else if (cmd == utf8str_tolower(sClearCommand))
		{
			LLFloaterChat* nearby_chat = LLFloaterChat::getInstance();
			if (nearby_chat)
			{
				if (LLViewerTextEditor* editor = nearby_chat->getChild<LLViewerTextEditor>("Chat History Editor"))
					editor->clear();
				if (LLViewerTextEditor* editor = nearby_chat->getChild<LLViewerTextEditor>("Chat History Editor with mute"))
					editor->clear();
				return false;
			}
		}
		else if (cmd == "/droll")
		{
			S32 dice_sides;
			if (!(input >> dice_sides))
				dice_sides = 6;
			LLSD args;
			args["RESULT"] = ((ll_rand() % dice_sides) + 1);
			LLNotificationsUtil::add("ChatCommandDiceRoll", args);
			return false;
		}
		else if (cmd == utf8str_tolower(sRegionMsgCommand)) // Region Message / Dialog
		{
			if (data.length() > cmd.length() + 1)
			{
				std::string notification_message = data.substr(cmd.length() + 1);
				if (!gAgent.getRegion()->isEstateManager())
				{
					gChatBar->sendChatFromViewer(notification_message, CHAT_TYPE_REGION, false);
					return false;
				}
				std::vector<std::string> strings(5, "-1");
				// [0] grid_x, unused here
				// [1] grid_y, unused here
				strings[2] = gAgentID.asString(); // [2] agent_id of sender
				// [3] sender name
				std::string name;
				LLAgentUI::buildFullname(name);
				strings[3] = name;
				strings[4] = notification_message; // [4] message
				LLRegionInfoModel::sendEstateOwnerMessage(gMessageSystem, "simulatormessage", LLFloaterRegionInfo::getLastInvoice(), strings);
			}
			return false;
		}
		else if (cmd == utf8str_tolower(sTeleportToCam))
		{
			gAgent.teleportViaLocation(gAgentCamera.getCameraPositionGlobal());
			return false;
		}
		else if (cmd == utf8str_tolower(sHoverHeight)) // Hover height
		{
			F32 height;
			if (input >> height)
			{
				gSavedPerAccountSettings.set("AvatarHoverOffsetZ",
											llclamp<F32>(height, MIN_HOVER_Z, MAX_HOVER_Z));
				return false;
			}
		}
		else if (cmd == utf8str_tolower(sResyncAnimCommand)) // Resync Animations
		{
			for (S32 i = 0; i < gObjectList.getNumObjects(); i++)
			{
				LLViewerObject* object = gObjectList.getObject(i);
				if (object && object->isAvatar())
				{
					LLVOAvatar& avatarp = *(LLVOAvatar*)object;
					for (LLVOAvatar::AnimIterator it = avatarp.mPlayingAnimations.begin(), end = avatarp.mPlayingAnimations.end(); it != end; ++it)
					{
						const std::pair<LLUUID, S32>& playpair = *it;
						avatarp.stopMotion(playpair.first, TRUE);
						avatarp.startMotion(playpair.first);
					}
				}
			}
			return false;
		}
		else if (cmd == utf8str_tolower(sKeyToName))
		{
			LLUUID targetKey;
			if (input >> targetKey)
			{
				LLAvatarName av_name;
				if (!LLAvatarNameCache::get(targetKey, &av_name))
				{
					LLAvatarNameCache::get(targetKey, boost::bind(spew_key_to_name, _1, _2));
					return false;
				}
				spew_key_to_name(targetKey, av_name);
			}
			return false;
		}
		else if (cmd == utf8str_tolower(sOfferTp))
		{
			std::string avatarKey;
//			LL_INFOS() << "CMD DEBUG 0 " << cmd << " " << avatarName << LL_ENDL;
			if (input >> avatarKey)
			{
//			LL_INFOS() << "CMD DEBUG 0 afterif " << cmd << " " << avatarName << LL_ENDL;
				LLUUID tempUUID;
				if (LLUUID::parseUUID(avatarKey, &tempUUID))
				{
					char buffer[DB_IM_MSG_BUF_SIZE * 2];  /* Flawfinder: ignore */
					std::string tpMsg="Join me!";
					LLMessageSystem* msg = gMessageSystem;
					msg->newMessageFast(_PREHASH_StartLure);
					msg->nextBlockFast(_PREHASH_AgentData);
					msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
					msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
					msg->nextBlockFast(_PREHASH_Info);
					msg->addU8Fast(_PREHASH_LureType, (U8)0); 

					msg->addStringFast(_PREHASH_Message, tpMsg);
					msg->nextBlockFast(_PREHASH_TargetData);
					msg->addUUIDFast(_PREHASH_TargetID, tempUUID);
					gAgent.sendReliableMessage();
					snprintf(buffer,sizeof(buffer),"Offered TP to key %s",tempUUID.asString().c_str());
					cmdline_printchat(std::string(buffer));
					return false;
				}
			}
		}
		else if (cmd == utf8str_tolower(sTP2Command))
		{
			if (data.length() > cmd.length() + 1) //Typing this cmd with no argument was causing a crash. -Madgeek
			{
				std::string name = data.substr(cmd.length()+1);
				cmdline_tp2name(name);
			}
			return false;
		}
		else if (cmd == utf8str_tolower(sAwayCommand))
		{
			handle_fake_away_status(NULL);
			return false;
		}
		else if (cmd == utf8str_tolower(sURLCommand))
		{
			if (data.length() > cmd.length() + 1)
			{
				const std::string sub(data.substr(cmd.length()+1));
				LLUUID id;
				if (id.parseUUID(sub, &id))
				{
					LLAvatarActions::showProfile(id);
					return false;
				}
				LLUrlAction::clickAction(sub);
			}
			return false;
		}
		else if (cmd == "typingstop")
		{
			std::string text;
			if (input >> text)
			{
				gChatBar->sendChatFromViewer(text, CHAT_TYPE_STOP, FALSE);
			}
			return false;
		}
		else if (cmd == "invrepair")
		{
			invrepair();
			return false;
		}
#ifdef PROF_CTRL_CALLS
		else if (cmd == "dumpcalls")
		{
			LLControlGroup::key_iter it = LLControlGroup::beginKeys();
			LLControlGroup::key_iter end = LLControlGroup::endKeys();
			for(;it!=end;++it)
			{
				ProfCtrlListAccum list;
				LLControlGroup::getInstance(*it)->applyToAll(&list);
				std::sort(list.mVariableList.begin(),list.mVariableList.end(),sort_calls);
				LL_INFOS() << *it << ": lookup count (" << gFrameCount << "frames)" << LL_ENDL;
				for(U32 i = 0;i<list.mVariableList.size();i++)
				{
					if (list.mVariableList[i].second)
						LL_INFOS() << "  " << list.mVariableList[i].first << ":  " << list.mVariableList[i].second << " lookups, " << ((float)list.mVariableList[i].second / (float)gFrameCount) << "l/f\n" << LL_ENDL;
				}
			}
			return false;
		}
#endif //PROF_CTRL_CALLS
		else if(cmd == "opentex")
		{
			LLUUID image_id;

			if (input >> image_id)
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

			return false;
		}
	}
	return true;
}

//case insensitive search for avatar in draw distance
//TODO: make this use the avatar list floaters list so we have EVERYONE
// even if they are out of draw distance.
LLUUID cmdline_partial_name2key(std::string partial_name)
{
	std::vector<LLUUID> avatars;
	std::string av_name;
	LLStringUtil::toLower(partial_name);
	LLWorld::getInstance()->getAvatars(&avatars);
	for(std::vector<LLUUID>::const_iterator i = avatars.begin(); i != avatars.end(); ++i)
	{
		if (LLAvatarListEntry* entry = LLFloaterAvatarList::instanceExists() ? LLFloaterAvatarList::instance().getAvatarEntry(*i) : NULL)
			av_name = entry->getName();
		else if (gCacheName->getFullName(*i, av_name));
		else if (LLVOAvatar* avatarp = gObjectList.findAvatar(*i))
			av_name = avatarp->getFullname();
		else
			continue;
		LLStringUtil::toLower(av_name);
		if (av_name.find(partial_name) != std::string::npos)
			return *i;
	}
	return LLUUID::null;
}


void cmdline_tp2name(std::string target)
{
	LLUUID avkey = cmdline_partial_name2key(target);
	if (avkey.isNull())
	{
		cmdline_printchat("Avatar not found.");
		return;
	}
	LLFloaterAvatarList* avlist = LLFloaterAvatarList::instanceExists() ? LLFloaterAvatarList::getInstance() : NULL;
	LLVOAvatar* avatarp = gObjectList.findAvatar(avkey);
	if (avatarp)
	{
		LLVector3d pos = avatarp->getPositionGlobal();
		pos.mdV[VZ] += 2.0;
		gAgent.teleportViaLocation(pos);
	}
	else if (avlist)
	{
		LLAvatarListEntry* entry = avlist->getAvatarEntry(avkey);
		if (entry)
		{
			LLVector3d pos = entry->getPosition();
			pos.mdV[VZ] += 2.0;
			gAgent.teleportViaLocation(pos);
		}
	}
}

void cmdline_printchat(const std::string& message)
{
	LLChat chat(message);
	chat.mSourceType = CHAT_SOURCE_SYSTEM;
	if (rlv_handler_t::isEnabled()) chat.mRlvLocFiltered = chat.mRlvNamesFiltered = true;
	LLFloaterChat::addChat(chat);
}

static void fake_local_chat(LLChat chat, const std::string& name)
{
	chat.mFromName = name;
	chat.mText = name + chat.mText;
	LLFloaterChat::addChat(chat);
}
void fake_local_chat(std::string msg)
{
	bool action(LLStringUtil::startsWith(msg, "/me") && (msg[3] == ' ' || msg[3] == '\''));
	if (action) msg.erase(0, 4);
	LLChat chat((action ? " " : ": ") + msg);
	chat.mFromID = gAgentID;
	chat.mSourceType = CHAT_SOURCE_SYSTEM;
	if (rlv_handler_t::isEnabled()) chat.mRlvLocFiltered = chat.mRlvNamesFiltered = true;
	chat.mPosAgent = gAgent.getPositionAgent();
	chat.mURL = "secondlife:///app/agent/" + gAgentID.asString() + "/about";
	if (action) chat.mChatStyle = CHAT_STYLE_IRC;
	if (!LLAvatarNameCache::getNSName(gAgentID, chat.mFromName))
	{
		LLAvatarNameCache::get(gAgentID, boost::bind(fake_local_chat, chat, boost::bind(&LLAvatarName::getNSName, _2, main_name_system())));
	}
	else
	{
		chat.mText = chat.mFromName + chat.mText;
		LLFloaterChat::addChat(chat);
	}
}
