///
/// Copyright 2019-2020 Oliver Giles
///
/// This file is part of Laminar
///
/// Laminar is free software: you can redistribute it and/or modify
/// it under the terms of the GNU General Public License as published by
/// the Free Software Foundation, either version 3 of the License, or
/// (at your option) any later version.
///
/// Laminar is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with Laminar.  If not, see <http://www.gnu.org/licenses/>
///
#ifndef LAMINAR_RPC_H_
#define LAMINAR_RPC_H_

#include <capnp/ez-rpc.h>
#include <capnp/rpc-twoparty.h>
#include <capnp/rpc.capnp.h>

class Laminar;

class Rpc {
public:
    Rpc(Laminar&li);
    kj::Promise<void> accept(kj::Own<kj::AsyncIoStream>&& connection);

    capnp::Capability::Client rpcInterface;
};

#endif //LAMINAR_RPC_H_
