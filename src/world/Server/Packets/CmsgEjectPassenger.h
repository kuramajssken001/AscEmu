/*
Copyright (c) 2014-2018 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once
#include <cstdint>

#include "ManagedPacket.h"
#include "WorldPacket.h"

namespace AscEmu { namespace Packets
{
    class CmsgEjectPassenger : public ManagedPacket
    {
    public:
        uint64_t guid;

        CmsgEjectPassenger() : CmsgEjectPassenger(0)
        {
        }

        CmsgEjectPassenger(uint64_t guid) :
            ManagedPacket(CMSG_EJECT_PASSENGER, 0),
            guid(guid)
        {
        }

    protected:
        bool internalSerialise(WorldPacket& packet) override
        {
            return false;
        }

        bool internalDeserialise(WorldPacket& packet) override
        {
            packet >> guid;
            return true;
        }
    };
}}