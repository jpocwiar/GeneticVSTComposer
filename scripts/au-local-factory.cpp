// JUCE's AU host resolves a bundle's IDs through AudioComponentFindNext, which may
// select an older installed component. Register this exact factory in the validator
// process only. Keep the bundle loaded for the factory's entire lifetime.
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    [[noreturn]] void fail (const char* message)
    {
        std::fprintf (stderr, "LOCAL_AU_FACTORY_ERROR: %s\n", message);
        std::abort();
    }

    CFTypeRef property (CFDictionaryRef dictionary, CFStringRef key, CFTypeID type)
    {
        const auto value = CFDictionaryGetValue (dictionary, key);
        if (value == nullptr || CFGetTypeID (value) != type)
            fail ("Missing or invalid AudioComponents metadata");
        return value;
    }

    OSType fourcc (CFDictionaryRef dictionary, CFStringRef key)
    {
        const auto value = static_cast<CFStringRef> (property (dictionary, key, CFStringGetTypeID()));
        char text[32] {};
        if (! CFStringGetCString (value, text, sizeof (text), kCFStringEncodingASCII) || std::strlen (text) != 4)
            fail ("An AU type, subtype or manufacturer is not four ASCII characters");

        OSType result = 0;
        for (int i = 0; i < 4; ++i)
            result = (result << 8) | static_cast<unsigned char> (text[i]);
        return result;
    }

    __attribute__((constructor)) void registerLocalAU()
    {
        const auto* path = std::getenv ("AU_VALIDATE_BUNDLE");
        if (path == nullptr || *path == '\0')
            fail ("AU_VALIDATE_BUNDLE must name the exact component bundle");

        const auto url = CFURLCreateFromFileSystemRepresentation (nullptr, reinterpret_cast<const UInt8*> (path), std::strlen (path), true);
        if (url == nullptr)
            fail ("Cannot create the component URL");
        const auto bundle = CFBundleCreate (nullptr, url);
        CFRelease (url);
        if (bundle == nullptr || ! CFBundleLoadExecutable (bundle))
            fail ("Cannot load the exact component executable");

        const auto rawComponents = CFBundleGetValueForInfoDictionaryKey (bundle, CFSTR ("AudioComponents"));
        if (rawComponents == nullptr || CFGetTypeID (rawComponents) != CFArrayGetTypeID())
            fail ("The bundle has no AudioComponents array");
        const auto components = static_cast<CFArrayRef> (rawComponents);
        if (CFArrayGetCount (components) == 0)
            fail ("The bundle contains no AudioComponents");

        for (CFIndex i = 0; i < CFArrayGetCount (components); ++i)
        {
            const auto rawMetadata = CFArrayGetValueAtIndex (components, i);
            if (rawMetadata == nullptr || CFGetTypeID (rawMetadata) != CFDictionaryGetTypeID())
                fail ("Invalid AudioComponents entry");
            const auto metadata = static_cast<CFDictionaryRef> (rawMetadata);
            AudioComponentDescription description {};
            description.componentType = fourcc (metadata, CFSTR ("type"));
            description.componentSubType = fourcc (metadata, CFSTR ("subtype"));
            description.componentManufacturer = fourcc (metadata, CFSTR ("manufacturer"));
            const auto symbol = static_cast<CFStringRef> (property (metadata, CFSTR ("factoryFunction"), CFStringGetTypeID()));
            const auto factory = reinterpret_cast<AudioComponentFactoryFunction> (CFBundleGetFunctionPointerForName (bundle, symbol));
            const auto name = static_cast<CFStringRef> (property (metadata, CFSTR ("name"), CFStringGetTypeID()));
            UInt32 version = 0;
            const auto versionProperty = static_cast<CFNumberRef> (property (metadata, CFSTR ("version"), CFNumberGetTypeID()));
            if (! CFNumberGetValue (versionProperty, kCFNumberSInt32Type, &version))
                fail ("Invalid AudioComponent version");
            if (factory == nullptr)
                fail ("Cannot resolve the exact factory symbol");
            const auto local = AudioComponentRegister (&description, name, version, factory);
            if (local == nullptr)
                fail ("Cannot register the exact factory for this process");
            if (AudioComponentFindNext (nullptr, &description) != local)
                fail ("Registry lookup did not select the exact factory");

            std::fprintf (stderr, "LOCAL_AU_FACTORY: exact bundle %s (version %u) registered for this process only\n", path, version);
        }

        // The script must fail closed if dyld silently ignores the injected library.
        if (const auto* receipt = std::getenv ("AU_VALIDATE_RECEIPT"))
        {
            auto* file = std::fopen (receipt, "a");
            if (file == nullptr)
                fail ("Cannot write the process-local registration receipt");
            const bool written = std::fprintf (file, "%s\n", path) > 0;
            const bool closed = std::fclose (file) == 0;
            if (! written || ! closed)
                fail ("Cannot finish the process-local registration receipt");
        }
    }
}
