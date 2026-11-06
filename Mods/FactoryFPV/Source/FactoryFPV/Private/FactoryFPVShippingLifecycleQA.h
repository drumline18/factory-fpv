#pragma once
class UWorld;
class FOutputDevice;
class FFactoryFPVShippingLifecycleQA final
{
public:
    static void Begin(UWorld* World, FOutputDevice& Output);
};
