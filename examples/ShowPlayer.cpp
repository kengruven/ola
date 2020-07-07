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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * ShowPlayer.cpp
 * A simple show playback system.
 * Copyright (C) 2011 Simon Newton
 *
 * The data file is in the form:
 * universe-number channel1,channel2,channel3
 * delay-in-ms
 * universe-number channel1,channel2,channel3
 */

#include <errno.h>
#include <string.h>
#include <ola/Callback.h>
#include <ola/Logging.h>
#include <ola/StringUtils.h>
#include <ola/base/SysExits.h>
#include <ola/client/ClientWrapper.h>
#include <ola/client/OlaClient.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "examples/ShowPlayer.h"

using std::vector;
using std::string;
using ola::DmxBuffer;


ShowPlayer::ShowPlayer(const string &filename)
    : m_loader(filename),
      m_infinite_loop(false),
      m_iteration_remaining(0),
      m_loop_delay(0),
      m_start(0),
      m_stop(0) {
}

ShowPlayer::~ShowPlayer() {}

int ShowPlayer::Init() {
  if (!m_client.Setup()) {
    OLA_FATAL << "Client Setup failed";
    return ola::EXIT_UNAVAILABLE;
  }

  if (!m_loader.Load()) {
    return ola::EXIT_NOINPUT;
  }

  return ola::EXIT_OK;
}


int ShowPlayer::Playback(unsigned int iterations,
                         PlaybackTime duration,
                         PlaybackTime delay,
                         PlaybackTime start,
                         PlaybackTime stop) {
  m_infinite_loop = iterations == 0 || duration != 0;
  m_iteration_remaining = iterations;
  m_loop_delay = delay;
  m_start = start;
  m_stop = stop;

  ola::io::SelectServer *ss = m_client.GetSelectServer();
  if (duration != 0) {
    ss->RegisterSingleTimeout(
       duration * 1000,
       ola::NewSingleCallback(ss, &ola::io::SelectServer::Terminate));
  }

  if ((SeekTo(m_start) != ShowLoader::State::OK)) {
    return ola::EXIT_DATAERR;
  }
  ss->Run();
  return ola::EXIT_OK;
}


/**
 * Begin playback from start point
 */
void ShowPlayer::Loop() {
  ShowLoader::State state = SeekTo(m_start);
  if (state != ShowLoader::State::OK) {
    m_client.GetSelectServer()->Terminate();
  }
}


/**
 * Seek to @p seek_time in the show file
 * @param seek_time the time (in milliseconds) to seek to
 */
ShowLoader::State ShowPlayer::SeekTo(PlaybackTime seek_time) {
  // Seeking to a time before the playhead's position requires moving from the
  // beginning of the file.  This could be optimized more if this happens
  // frequently.
  if (seek_time < m_playback_pos) {
    m_loader.Reset();
    m_playback_pos = 0;
  }

  // Keep reading through the show file until desired time is reached.
  ShowEntry entry;
  PlaybackTime playhead_time = m_playback_pos;
  ShowLoader::State state;
  bool found = false;
  while (true) {
    state = m_loader.NextEntry(&entry);
    switch (state) {
      case ShowLoader::END_OF_FILE:
        OLA_FATAL << "Show file ends before the start time (Actual length "
                  << m_playback_pos << " ms)";
        return state;
      case ShowLoader::INVALID_LINE:
        HandleInvalidLine();
        return state;
      default: {
      }
    }
    playhead_time += entry.next_wait;
    if (!found && playhead_time == seek_time) {
      // Use the next frame if landing on the trailing edge of a frame's timeout
      found = true;
      continue;
    }
    if (found || playhead_time > seek_time) {
      break;
    }
  }
  // Adjust the timeout to handle landing in the middle of the entry's timeout
  m_playback_pos = seek_time;
  entry.next_wait = playhead_time - seek_time;
  SendEntry(entry);

  return ShowLoader::OK;
}


/**
 * Send the next frame in the show file
 */
void ShowPlayer::SendNextFrame() {
  ShowEntry entry;
  ShowLoader::State state = m_loader.NextEntry(&entry);
  if (state == ShowLoader::END_OF_FILE ||
      (m_stop > 0 && m_playback_pos >= m_stop)) {
    if (entry.buffer.Size() > 0) {
      // Send the last frame before looping/exiting
      SendFrame(entry);
    }
    HandleEndOfFile();
    return;
  } else if (state == ShowLoader::INVALID_LINE) {
    HandleInvalidLine();
    return;
  }
  SendEntry(entry);
}


/**
 * Send @p entry and wait for next
 * @param entry the show file entry to send
 */
void ShowPlayer::SendEntry(const ShowEntry &entry) {
  // Send DMX data
  SendFrame(entry);

  // Set when next to send data
  OLA_INFO << "Registering timeout for " << entry.next_wait << "ms";
  m_playback_pos += entry.next_wait;
  m_client.GetSelectServer()->RegisterSingleTimeout(
     entry.next_wait,
     ola::NewSingleCallback(this, &ShowPlayer::SendNextFrame));
}


void ShowPlayer::SendFrame(const ShowEntry &entry) const {
  OLA_INFO << "Universe: " << entry.universe << ": " << entry.buffer.ToString();
  ola::client::SendDMXArgs args;
  m_client.GetClient()->SendDMX(entry.universe, entry.buffer, args);
}


/**
 * Handle the case where we reach the end of file
 */
void ShowPlayer::HandleEndOfFile() {
  if (m_stop > m_playback_pos) {
    OLA_WARN << "Show file ends before the stop time (Actual length "
             << m_playback_pos << " ms)";
  }
  m_iteration_remaining--;
  if (m_infinite_loop || m_iteration_remaining > 0) {
    OLA_INFO << "----- Waiting " << m_loop_delay << " ms before looping -----";
    // Move to start point and send the frame
    m_client.GetSelectServer()->RegisterSingleTimeout(
       m_loop_delay,
       ola::NewSingleCallback(this, &ShowPlayer::Loop));
    return;
  } else {
    // stop the show
    m_client.GetSelectServer()->Terminate();
  }
}


/**
 * Handle reading an invalid line from the show file
 */
void ShowPlayer::HandleInvalidLine() {
  OLA_FATAL << "Invalid data at line " << m_loader.GetCurrentLineNumber();
  m_client.GetSelectServer()->Terminate();
}
