/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * SPIDevice.cpp
 * SPI device
 * Copyright (C) 2013 Simon Newton
 */

#include <sstream>
#include <string>
#include <vector>

#include "ola/Logging.h"
#include "ola/StringUtils.h"
#include "ola/network/NetworkUtils.h"
#include "olad/PluginAdaptor.h"
#include "olad/Preferences.h"
#include "olad/Universe.h"
#include "plugins/spi/SPIDevice.h"
#include "plugins/spi/SPIPort.h"
#include "plugins/spi/SPIPlugin.h"

namespace ola {
namespace plugin {
namespace spi {

const char SPIDevice::SPI_DEVICE_NAME[] = "SPI Plugin";

/*
 * Create a new device
 */
SPIDevice::SPIDevice(SPIPlugin *owner,
                     Preferences *prefs,
                     PluginAdaptor *plugin_adaptor,
                     const string &spi_device,
                     const UID &uid,
                     uint8_t pixel_count)
    : Device(owner, SPI_DEVICE_NAME),
      m_preferences(prefs),
      m_plugin_adaptor(plugin_adaptor) {
  m_port = new SPIOutputPort(this, spi_device, uid, pixel_count);
}



string SPIDevice::DeviceId() const {
  return m_port->Description();
}

/*
 * Start this device
 */
bool SPIDevice::StartHook() {
  if (!m_port->Init())
    return false;
  AddPort(m_port);
  return true;
}
}  // spi
}  // plugin
}  // ola
