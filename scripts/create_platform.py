import vitis

client = vitis.create_client()
client.set_workspace(".")

client.create_platform_component(
    name="PYNQ_Platform",
    hw_design="$COMPONENT_LOCATION/../SPI_UART_GPIO_wrapper5.xsa",
    os="standalone",
    cpu="ps7_cortexa9_0",
    domain_name="standalone_ps7_cortexa9_0",
    compiler="gcc"
)

platform = client.get_component("PYNQ_Platform")

domain = platform.get_domain(name="zynq_fsbl")
status = domain.set_lib(lib_name="lwip220", path="/tools/Xilinx/2025.2/Vitis/data/embeddedsw/ThirdParty/sw_services/lwip220_v1_3")

domain = platform.get_domain(name="standalone_ps7_cortexa9_0")
status = domain.set_lib(lib_name="lwip220", path="/tools/Xilinx/2025.2/Vitis/data/embeddedsw/ThirdParty/sw_services/lwip220_v1_3")

platform.build()
vitis.dispose()
