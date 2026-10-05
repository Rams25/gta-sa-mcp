// Grabbing the frame the game just drew and encoding it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace capture
{

struct Image
{
	int width = 0, height = 0;
	std::vector<std::uint8_t> rgb; // 3 bytes per pixel, top row first
};

// Copies the back buffer. Game thread, between the end of the scene and its presentation.
// On failure returns false and fills `error`.
bool Grab(Image& out, std::string& error);

// Shrinks the image (keeping its proportions) when it is wider than `maxWidth`.
void Downscale(Image& image, int maxWidth);

// `format`: "jpeg" or "png". Returns the encoded file, empty on failure.
std::vector<std::uint8_t> Encode(const Image& image, const std::string& format, int jpegQuality);

std::string Base64(const std::vector<std::uint8_t>& data);

}
