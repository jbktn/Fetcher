// Conversion of the data model to and from its JSON file format.
#pragma once

#include "model.h"

#include <string>
#include <vector>

namespace fetcher {

// All parse functions throw std::runtime_error on malformed input, including
// a file written by a newer, incompatible version of the app.
std::string folderToJson(const Folder& root);
Folder folderFromJson(const std::string& text);

std::string historyToJson(const std::vector<HistoryEntry>& entries);
std::vector<HistoryEntry> historyFromJson(const std::string& text);

}  // namespace fetcher
