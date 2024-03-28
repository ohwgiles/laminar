///
/// Copyright 2015-2022 Oliver Giles
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
#include "json.h"

template<> Json& Json::set(const char* key, double value) { String(key); Double(value); return *this; }
template<> Json& Json::set(const char* key, const char* value) { String(key); String(value); return *this; }
template<> Json& Json::set(const char* key, std::string value) { String(key); String(value.c_str()); return *this; }

Json& Json::setJsonObject(const char* key, const std::string& object)
{
   String(key);
   if( object.length() )
   {
      RawValue(object.c_str(), object.length(), rapidjson::kObjectType);
   }
   else
   {
      StartObject();
      EndObject();
   }
   return *this;
}

