#pragma once

#include <QString>
#include <QUrl>

#include "lumos/tools.hpp"

QUrl link_url(const lumos::LinkTarget& target, const QString& note_path);
