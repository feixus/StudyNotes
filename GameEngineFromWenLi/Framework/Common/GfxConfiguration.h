#pragma once
#include <cstdint>
#include <iostream>

namespace My
{
    struct GfxConfiguration {
        GfxConfiguration(uint32_t r = 8, uint32_t g = 8,
                uint32_t b = 8, uint32_t a = 8,
                uint32_t d = 24, uint32_t s = 0, uint32_t msaa = 0,
                uint32_t width = 1920, uint32_t height = 1080, const wchar_t* app_name = L"GameEngine") :
                redBits(r), greenBits(g), blueBits(b), alphaBits(a),
                depthBits(r), stencilBits(s), msaaSamples(msaa),
                screenWidth(width), screenHeight(height), appName(app_name)
        {}

        uint32_t redBits; ///< red color channel in bits
		uint32_t greenBits; ///< green color channel in bits
		uint32_t blueBits; ///< blue color channel in bits
		uint32_t alphaBits; ///< alpha color channel in bits
		uint32_t depthBits; ///< depth buffer in bits
		uint32_t stencilBits; ///< stencil buffer in bits
		uint32_t msaaSamples; ///< MSAA samples
		uint32_t screenWidth;
		uint32_t screenHeight;
        const wchar_t* appName;

        // friend for non-member function or another class to acess private and protected members of the class
        friend std::wostream& operator<<(std::wostream& out, const GfxConfiguration& config)
        {
            out << "App Name: " << config.appName << std::endl; 
            out << "GfxConfiguration:" <<
                " R:" << config.redBits <<
                " G:" << config.greenBits <<
                " B:" << config.blueBits <<
                " A:" << config.alphaBits <<
                " D:" << config.depthBits <<
                " S:" << config.stencilBits <<
                " MSAA:" << config.msaaSamples <<
                " W:" << config.screenWidth <<
                " H:" << config.screenHeight <<
                std::endl;

            return out;
        }
    };

}
