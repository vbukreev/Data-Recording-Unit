#include <iostream>
#include "DataAcquisition.h"

int main() {
    DataAcquisition data_acquisition_unit;
    data_acquisition_unit.executeData();
    data_acquisition_unit.shutdownDataAcquisition();
    return 0;
}