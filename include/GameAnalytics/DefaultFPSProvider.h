#pragma once

#include "GameAnalytics/IFPSProvider.h"

namespace gameanalytics
{
    class DefaultFPSProvider : public IFPSProvider
    {
    public:

        // Constructor
        DefaultFPSProvider() {}

        // Destructor
        virtual ~DefaultFPSProvider() {};

        // Method to get the current FPS, polled every 1 second
        virtual float getCurrentFPS() const override {
            return 1.0f;
        }
    };
}
