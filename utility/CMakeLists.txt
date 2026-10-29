########################################################################
# Utility blocks that operate on SDR streams
########################################################################
soapy_module(
    TARGET SoapyUtilityBlocks
    SOURCES
        ChannelAligner.cpp
        Converter.cpp
        RandomDropper.cpp
        TxBurstTimer.cpp
        TestConverter.cpp
    TESTS
        test_converter
)
