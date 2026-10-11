#pragma once
#include <sstream>
#include <string>
#include <vector>

namespace telemetry_fixture {
inline std::vector<std::vector<std::string>> cameraRows(const std::string &text) {
  std::vector<std::vector<std::string>> rows;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.compare(0, 7, "camera,") != 0)
      continue;
    std::vector<std::string> row;
    std::istringstream fields(line);
    std::string field;
    while (std::getline(fields, field, ','))
      row.push_back(field);
    rows.push_back(row);
  }
  return rows;
}
} // namespace telemetry_fixture
