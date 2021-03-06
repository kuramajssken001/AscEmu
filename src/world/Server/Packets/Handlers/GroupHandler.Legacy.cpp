/*
 * AscEmu Framework based on ArcEmu MMORPG Server
 * Copyright (c) 2014-2018 AscEmu Team <http://www.ascemu.org>
 * Copyright (C) 2008-2012 ArcEmu Team <http://www.ArcEmu.org/>
 * Copyright (C) 2005-2007 Ascent Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "StdAfx.h"
#include "Map/MapMgr.h"
#include "Spell/SpellAuras.h"
#include "Map/WorldCreator.h"
#include "Objects/ObjectMgr.h"
#include "Units/Creatures/Pet.h"
#include "Server/Packets/CmsgGroupInvite.h"
#include "Server/Packets/SmsgGroupInvite.h"

using namespace AscEmu::Packets;

#if VERSION_STRING != Cata
//MIT
void WorldSession::handleGroupInviteOpcode(WorldPacket& recvPacket)
{
    CmsgGroupInvite recv_packet;
    if (!recv_packet.deserialise(recvPacket))
        return;

    auto player = objmgr.GetPlayer(recv_packet.name.c_str(), false);
    if (player == nullptr)
    {
        SendPartyCommandResult(_player, 0, recv_packet.name, ERR_PARTY_CANNOT_FIND);
        return;
    }

    if (player == _player || _player->HasBeenInvited())
        return;

    if (_player->InGroup() && !_player->IsGroupLeader())
    {
        SendPartyCommandResult(_player, 0, "", ERR_PARTY_YOU_ARE_NOT_LEADER);
        return;
    }

    if (_player->GetGroup() != nullptr)
    {
        if (_player->GetGroup()->IsFull())
        {
            SendPartyCommandResult(_player, 0, "", ERR_PARTY_IS_FULL);
            return;
        }
    }

    if (player->InGroup())
    {
        SendPartyCommandResult(_player, player->GetGroup()->getGroupType(), recv_packet.name, ERR_PARTY_ALREADY_IN_GROUP);
        player->GetSession()->SendPacket(SmsgGroupInvite(0, GetPlayer()->getName().c_str()).serialise().get());
        return;
    }

    if (player->GetTeam() != _player->GetTeam() && _player->GetSession()->GetPermissionCount() == 0 && !worldConfig.player.isInterfactionGroupEnabled)
    {
        SendPartyCommandResult(_player, 0, recv_packet.name, ERR_PARTY_WRONG_FACTION);
        return;
    }

    if (player->HasBeenInvited())
    {
        SendPartyCommandResult(_player, 0, recv_packet.name, ERR_PARTY_ALREADY_IN_GROUP);
        return;
    }

    if (player->Social_IsIgnoring(_player->getGuidLow()))
    {
        SendPartyCommandResult(_player, 0, recv_packet.name, ERR_PARTY_IS_IGNORING_YOU);
        return;
    }

    if (player->isGMFlagSet() && !_player->GetSession()->HasPermissions())
    {
        SendPartyCommandResult(_player, 0, recv_packet.name, ERR_PARTY_CANNOT_FIND);
        return;
    }

    player->GetSession()->SendPacket(SmsgGroupInvite(1, GetPlayer()->getName().c_str()).serialise().get());

    SendPartyCommandResult(_player, 0, recv_packet.name, ERR_PARTY_NO_ERROR);

    player->SetInviter(_player->getGuidLow());
}

//MIT end

////////////////////////////////////////////////////////////////
///This function handles CMSG_GROUP_CANCEL:
////////////////////////////////////////////////////////////////
void WorldSession::HandleGroupCancelOpcode(WorldPacket& /*recvPacket*/)
{
    CHECK_INWORLD_RETURN;

    LOG_DEBUG("WORLD: received CMSG_GROUP_CANCEL");
}

////////////////////////////////////////////////////////////////
///This function handles CMSG_GROUP_ACCEPT:
////////////////////////////////////////////////////////////////
void WorldSession::HandleGroupAcceptOpcode(WorldPacket& /*recv_data*/)
{
    CHECK_INWORLD_RETURN;

    // we are in group already
    if (_player->GetGroup() != NULL)
        return;

    Player* player = objmgr.GetPlayer(_player->GetInviter());
    if (!player)
        return;

    player->SetInviter(0);
    _player->SetInviter(0);

    Group* grp = player->GetGroup();
    if (grp != NULL)
    {
        grp->AddMember(_player->m_playerInfo);
        _player->iInstanceType = grp->m_difficulty;
        _player->SendDungeonDifficulty();

        //sInstanceSavingManager.ResetSavedInstancesForPlayer(_player);
        return;
    }

    // If we're this far, it means we have no existing group, and have to make one.
    grp = new Group(true);
    grp->m_difficulty = player->iInstanceType;
    grp->AddMember(player->m_playerInfo);        // add the inviter first, therefore he is the leader
    grp->AddMember(_player->m_playerInfo);    // add us.
    _player->iInstanceType = grp->m_difficulty;
    _player->SendDungeonDifficulty();

    Instance* instance = sInstanceMgr.GetInstanceByIds(player->GetMapId(), player->GetInstanceID());
    if (instance != NULL && instance->m_creatorGuid == player->getGuidLow())
    {
        grp->m_instanceIds[instance->m_mapId][instance->m_difficulty] = instance->m_instanceId;
        instance->m_creatorGroup = grp->GetID();
        instance->m_creatorGuid = 0;
        instance->SaveToDB();
    }
}

///////////////////////////////////////////////////////////////////////////////////////
///This function handles CMSG_GROUP_DECLINE:
//////////////////////////////////////////////////////////////////////////////////////
void WorldSession::HandleGroupDeclineOpcode(WorldPacket& /*recv_data*/)
{
    CHECK_INWORLD_RETURN;
    WorldPacket data(SMSG_GROUP_DECLINE, 100);

    Player* player = objmgr.GetPlayer(_player->GetInviter());
    if (!player)
        return;

    data << GetPlayer()->getName().c_str();

    player->GetSession()->SendPacket(&data);
    player->SetInviter(0);
    _player->SetInviter(0);
}

//////////////////////////////////////////////////////////////////////////////////////////
///This function handles CMSG_GROUP_UNINVITE(unused since 3.1.3):
//////////////////////////////////////////////////////////////////////////////////////////
void WorldSession::HandleGroupUninviteOpcode(WorldPacket& recv_data)
{
    CHECK_INWORLD_RETURN;
    CHECK_PACKET_SIZE(recv_data, 1);
    std::string membername;
    Group* group;
    Player* player;
    PlayerInfo* info;

    recv_data >> membername;

    player = objmgr.GetPlayer(membername.c_str(), false);
    info = objmgr.GetPlayerInfoByName(membername.c_str());
    if (player == NULL && info == NULL)
    {
        SendPartyCommandResult(_player, 0, membername, ERR_PARTY_CANNOT_FIND);
        return;
    }

    if (!_player->InGroup() || (info != NULL && info->m_Group != _player->GetGroup()))
    {
        SendPartyCommandResult(_player, 0, membername, ERR_PARTY_IS_NOT_IN_YOUR_PARTY);
        return;
    }

    if (!_player->IsGroupLeader())
    {
        if (player == NULL)
        {
            SendPartyCommandResult(_player, 0, membername, ERR_PARTY_CANNOT_FIND);
            return;
        }
        else if (_player != player)
        {
            SendPartyCommandResult(_player, 0, "", ERR_PARTY_YOU_ARE_NOT_LEADER);
            return;
        }
    }

    group = _player->GetGroup();

    if (group != NULL)
    {
        group->RemovePlayer(info);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
///This function handles CMSG_GROUP_UNINVITE_GUID(used since 3.1.3):
//////////////////////////////////////////////////////////////////////////////////////////
void WorldSession::HandleGroupUninviteGuidOpcode(WorldPacket& recv_data)
{
    CHECK_INWORLD_RETURN;
    CHECK_PACKET_SIZE(recv_data, 1);
    uint64 PlayerGUID;
    std::string membername = "unknown";
    Group* group;
    Player* player;
    PlayerInfo* info;

    recv_data >> PlayerGUID;

    player = objmgr.GetPlayer(Arcemu::Util::GUID_LOPART(PlayerGUID));
    info = objmgr.GetPlayerInfo(Arcemu::Util::GUID_LOPART(PlayerGUID));
    // If both conditions match the player gets thrown out of the group by the server since this means the character is deleted
    if (player == NULL && info == NULL)
    {
        SendPartyCommandResult(_player, 0, membername, ERR_PARTY_CANNOT_FIND);
        return;
    }

    membername = player ? player->getName().c_str() : info->name;

    if (!_player->InGroup() || (info != NULL && info->m_Group != _player->GetGroup()))
    {
        SendPartyCommandResult(_player, 0, membername, ERR_PARTY_IS_NOT_IN_YOUR_PARTY);
        return;
    }

    if (!_player->IsGroupLeader())
    {
        if (player == NULL)
        {
            SendPartyCommandResult(_player, 0, membername, ERR_PARTY_CANNOT_FIND);
            return;
        }
        else if (_player != player)
        {
            SendPartyCommandResult(_player, 0, "", ERR_PARTY_YOU_ARE_NOT_LEADER);
            return;
        }
    }

    group = _player->GetGroup();
    if (group != NULL)
    {
        group->RemovePlayer(info);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
///This function handles CMSG_GROUP_SET_LEADER:
//////////////////////////////////////////////////////////////////////////////////////////
void WorldSession::HandleGroupSetLeaderOpcode(WorldPacket& recv_data)
{
    CHECK_INWORLD_RETURN;
    // important note _player->GetName() can be wrong.
    CHECK_PACKET_SIZE(recv_data, 1);
    WorldPacket data;
    uint64 MemberGuid;
    Player* player;

    recv_data >> MemberGuid;

    player = objmgr.GetPlayer((uint32)MemberGuid);

    if (player == NULL)
    {
        //SendPartyCommandResult(_player, 0, membername, ERR_PARTY_CANNOT_FIND);
        SendPartyCommandResult(_player, 0, _player->getName().c_str(), ERR_PARTY_CANNOT_FIND);
        return;
    }

    if (!_player->IsGroupLeader())
    {
        SendPartyCommandResult(_player, 0, "", ERR_PARTY_YOU_ARE_NOT_LEADER);
        return;
    }

    if (player->GetGroup() != _player->GetGroup())
    {
        //SendPartyCommandResult(_player, 0, membername, ERR_PARTY_IS_NOT_IN_YOUR_PARTY);
        SendPartyCommandResult(_player, 0, _player->getName().c_str(), ERR_PARTY_IS_NOT_IN_YOUR_PARTY);
        return;
    }

    Group* pGroup = _player->GetGroup();
    if (pGroup)
        pGroup->SetLeader(player, false);
}

//////////////////////////////////////////////////////////////////////////////////////////
///This function handles CMSG_GROUP_DISBAND:
//////////////////////////////////////////////////////////////////////////////////////////
void WorldSession::HandleGroupDisbandOpcode(WorldPacket& /*recv_data*/)
{
    // this is actually leaving a party, disband is not possible anymore
    CHECK_INWORLD_RETURN;
    Group* pGroup = _player->GetGroup();
    if (pGroup == NULL)
        return;

    // cant leave a battleground group (blizzlike 3.3.3)
    if (pGroup->getGroupType() & GROUP_TYPE_BG)
        return;

    pGroup->RemovePlayer(_player->m_playerInfo);
}

//////////////////////////////////////////////////////////////////////////////////////////
///This function handles CMSG_LOOT_METHOD:
//////////////////////////////////////////////////////////////////////////////////////////
void WorldSession::HandleLootMethodOpcode(WorldPacket& recv_data)
{
    CHECK_INWORLD_RETURN;
    CHECK_PACKET_SIZE(recv_data, 16);

    uint32 lootMethod;
    uint64 lootMaster;
    uint32 threshold;

    recv_data >> lootMethod;
    recv_data >> lootMaster;
    recv_data >> threshold;

    if (!_player->IsGroupLeader())
    {
        SendPartyCommandResult(_player, 0, "", ERR_PARTY_YOU_ARE_NOT_LEADER);
        return;
    }

    Group* pGroup = _player->GetGroup();

    if (pGroup == NULL)
        return;

    Player* plr = objmgr.GetPlayer((uint32)lootMaster);
    if (plr != NULL)
    {
        pGroup->SetLooter(plr, static_cast<uint8>(lootMethod), static_cast<uint16>(threshold));
    }
    else
    {
        pGroup->SetLooter(_player, static_cast<uint8>(lootMethod), static_cast<uint16>(threshold));
    }

}

void WorldSession::HandleMinimapPingOpcode(WorldPacket& recv_data)
{
    CHECK_INWORLD_RETURN;
    CHECK_PACKET_SIZE(recv_data, 8);
    if (!_player->InGroup())
        return;
    Group* party = _player->GetGroup();
    if (!party)return;

    float x, y;
    recv_data >> x;
    recv_data >> y;

    WorldPacket data;
    data.SetOpcode(MSG_MINIMAP_PING);
    data << _player->getGuid();
    data << x;
    data << y;
    party->SendPacketToAllButOne(&data, _player);
}

void WorldSession::HandleSetPlayerIconOpcode(WorldPacket& recv_data)
{
    CHECK_INWORLD_RETURN;
    uint64 guid;
    uint8 icon;
    Group* pGroup = _player->GetGroup();
    if (pGroup == NULL)
        return;

    recv_data >> icon;
    if (icon == 0xFF)
    {
        // client request
        WorldPacket data(MSG_RAID_TARGET_UPDATE, 73);
        data << uint8(1);
        for (uint8 i = 0; i < 8; ++i)
            data << i << pGroup->m_targetIcons[i];

        SendPacket(&data);
    }
    else if (_player->IsGroupLeader())
    {
        recv_data >> guid;
        if (icon > 7)
            return;            // whoops, buffer overflow :p

        //removing other icon
        for (uint8 i = 0; i < 8; ++i)
        {
            if (pGroup->m_targetIcons[i] == guid)
            {
                WorldPacket data(MSG_RAID_TARGET_UPDATE, 10);
                data << uint8(0);
                data << uint64(0);
                data << uint8(i);
                data << uint64(0);
                pGroup->SendPacketToAll(&data);

                pGroup->m_targetIcons[i] = 0;
                break;
            }
        }
        // setting icon
        WorldPacket data(MSG_RAID_TARGET_UPDATE, 10);
        data << uint8(0);
        data << uint64(GetPlayer()->getGuid());
        data << icon;
        data << guid;
        pGroup->SendPacketToAll(&data);

        pGroup->m_targetIcons[icon] = guid;
    }
}

void WorldSession::SendPartyCommandResult(Player* pPlayer, uint32 p1, std::string name, uint32 err)
{
    CHECK_INWORLD_RETURN;
    // if error message do not work, please sniff it and leave me a message
    if (pPlayer)
    {
        WorldPacket data;
        data.Initialize(SMSG_PARTY_COMMAND_RESULT);
        data << p1;
        if (!name.length())
            data << uint8(0);
        else
            data << name.c_str();

        data << err;
        pPlayer->GetSession()->SendPacket(&data);
    }
}

void WorldSession::HandlePartyMemberStatsOpcode(WorldPacket& recv_data)
{
    CHECK_INWORLD_RETURN

    uint64 guid;
    recv_data >> guid;

    Player* plr = _player->GetMapMgr()->GetPlayer((uint32)guid);

    if (!_player->GetGroup() || !plr)
    {
        WorldPacket data(SMSG_PARTY_MEMBER_STATS_FULL, 3 + 4 + 2);
        data << uint8(0);                                   // only for SMSG_PARTY_MEMBER_STATS_FULL, probably arena/bg related
        data.appendPackGUID(guid);
        data << uint32(GROUP_UPDATE_FLAG_STATUS);
        data << uint16(MEMBER_STATUS_OFFLINE);
        SendPacket(&data);
        return;
    }

    if (!_player->GetGroup()->HasMember(plr))
        return;

    if (_player->IsVisible(plr->getGuid()))
        return;

    Pet* pet = plr->GetSummon();

    WorldPacket data(SMSG_PARTY_MEMBER_STATS_FULL, 4 + 2 + 2 + 2 + 1 + 2 * 6 + 8 + 1 + 8);
    data << uint8(0);                                       // only for SMSG_PARTY_MEMBER_STATS_FULL, probably arena/bg related
    data.append(plr->GetNewGUID());

    uint32 mask1 = 0x00040BFF;                              // common mask, real flags used 0x000040BFF
    if (pet)
        mask1 = 0x7FFFFFFF;                                 // for hunters and other classes with pets

    uint8 powerType = plr->getPowerType();
    data << uint32(mask1);
    data << uint16(MEMBER_STATUS_ONLINE);
    data << uint32(plr->getHealth());
    data << uint32(plr->getMaxHealth());
    data << uint8(powerType);
    data << uint16(plr->GetPower(powerType));
    data << uint16(plr->GetMaxPower(powerType));
    data << uint16(plr->getLevel());
    data << uint16(plr->GetZoneId());
    data << uint16(plr->GetPositionX());
    data << uint16(plr->GetPositionY());

    uint64 auramask = 0;
    size_t maskPos = data.wpos();
    data << uint64(auramask);
    for (uint8 i = 0; i < 64; ++i)
    {
        if (Aura * aurApp = plr->GetAuraWithSlot(i))
        {
            auramask |= (uint64(1) << i);
            data << uint32(aurApp->GetSpellId());
            data << uint8(1);
        }
    }
    data.put<uint64>(maskPos, auramask);

    if (pet)
    {
        uint8 petpowertype = pet->getPowerType();
        data << uint64(pet->getGuid());
        data << pet->GetName();
        data << uint16(pet->getDisplayId());
        data << uint32(pet->getHealth());
        data << uint32(pet->getMaxHealth());
        data << uint8(petpowertype);
        data << uint16(pet->GetPower(petpowertype));
        data << uint16(pet->GetMaxPower(petpowertype));

        uint64 petauramask = 0;
        size_t petMaskPos = data.wpos();
        data << uint64(petauramask);
        for (uint8 i = 0; i < 64; ++i)
        {
            if (Aura * auraApp = pet->GetAuraWithSlot(i))
            {
                petauramask |= (uint64(1) << i);
                data << uint32(auraApp->GetSpellId());
                data << uint8(1);
            }
        }
        data.put<uint64>(petMaskPos, petauramask);
    }
    else
    {
        data << uint8(0);      // GROUP_UPDATE_FLAG_PET_NAME
        data << uint64(0);     // GROUP_UPDATE_FLAG_PET_AURAS
    }

    SendPacket(&data);
}
#endif
