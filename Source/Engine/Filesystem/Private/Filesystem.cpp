#include <fstream>
#include <sstream>

#include "portable-file-dialogs.h"

#include "Engine/Filesystem/Filesystem.hpp"

std::optional<Filepath> Filesystem::ShowOpenDialog(const DialogDescription &description)
{
    pfd::open_file openDialog(description.title,
            description.defaultPath.GetAbsolute(),
            description.filters, false);

    if (!openDialog.result().empty())
    {
        return std::make_optional(Filepath(openDialog.result().front()));
    }

    return std::nullopt;
}

std::optional<Filepath> Filesystem::ShowSaveDialog(const DialogDescription &description)
{
    pfd::save_file saveDialog(description.title,
            description.defaultPath.GetAbsolute(),
            description.filters, true);

    if (!saveDialog.result().empty())
    {
        return std::make_optional(Filepath(saveDialog.result()));
    }

    return std::nullopt;
}

std::string Filesystem::ReadFile(const Filepath &filepath)
{
    const std::ifstream file(filepath.GetAbsolute());

    std::stringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}