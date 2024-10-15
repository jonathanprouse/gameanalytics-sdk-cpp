#pragma once

namespace gameanalytics
{

    class IFPSProvider
    {
    public:

        // Constructor
        IFPSProvider() {}
        
        // Destructor
        virtual ~IFPSProvider() {};

        // Method to get the current FPS, polled every 1 second
        virtual float getCurrentFPS() const = 0;
    };

}
