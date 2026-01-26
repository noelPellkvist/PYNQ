//#include "MCP4261.h"
#include <stdint.h>
#include <sys/_stdint.h>
#include <xil_types.h>

#include "xil_printf.h"
#include "xgpio.h"
#include "sleep.h"
#include <sys/types.h>


//NECESSARY!! for spi master initialization!!
int init_SPI(XSpi *spi_instance, XSpi_Config *spi_conf)
{
    int spi_status;
    spi_conf = XSpi_LookupConfig(XPAR_XSPI_0_BASEADDR);

    if(spi_conf == NULL){
        print("SPI device not found");
        return XST_FAILURE;
    }

    spi_status = XSpi_CfgInitialize(spi_instance, spi_conf,
				    spi_conf->BaseAddress);
	if (spi_status != XST_SUCCESS) {
        print("wrong122");
		return XST_FAILURE;
	}

    /*
	 * Set the Spi device as a master and in loopback mode.
	 */
	spi_status = XSpi_SetOptions(spi_instance, XSP_MASTER_OPTION |
				 //XSP_LOOPBACK_OPTION | 
                 XSP_MANUAL_SSELECT_OPTION);
	if (spi_status != XST_SUCCESS) {
        print("wrong 132");
		return XST_FAILURE;
	}

	spi_status = XSpi_Start(spi_instance);

    if (spi_status != XST_SUCCESS) {
        print("wrong 139");
		return XST_FAILURE;
	}
	XSpi_IntrGlobalDisable(spi_instance);

    return XST_SUCCESS;
}

//TEST functions is packaged based on MCP4261.h
//providing one-line code to test SPI on MCP4261

//ATTENTION!! the TEST_get_whatever just print the gotten value,
//to save the gotten value, see the get method in MCP4261.h

void TEST_set(MCP4261 *mcp_instance, u_int16_t resis_1, uint16_t resis_2, bool test_type, uint32_t sleep_time)
{
    if (!mcp_instance->set_wiper(0, resis_1, test_type)) printf("FAIL SET 0!! \n");
        usleep(sleep_time);
    if (!mcp_instance->set_wiper(1, resis_2, test_type)) printf("FAIL SET 1!! \n");
        usleep(sleep_time);
}

void TEST_get(MCP4261 *mcp_instance, bool test_type, uint32_t sleep_time){
        uint16_t MCP_read_value {0};
        if (!mcp_instance->get_wiper(0, MCP_read_value, test_type)) printf("FAIL READ 0!! \n");
        else printf("wiper 0 read value is %d \n", MCP_read_value);

        usleep(sleep_time);
        MCP_read_value=0;
        
        if (!mcp_instance->get_wiper(1, MCP_read_value, test_type)) printf("FAIL READ 1!! \n");
        else printf("wiper 1 read value is %d \n", MCP_read_value);
        usleep(sleep_time);
}


void TEST_get_SR(MCP4261 *mcp_instance, uint32_t sleep_time){
        uint8_t MCP_read_value {0};
        if (!mcp_instance->get_SR(MCP_read_value)) printf("FAIL READ SR!! \n");
        else printf("SR read value is %x \n", MCP_read_value);
        usleep(sleep_time);
}


void TEST_set_TCON(uint32_t sleep_time, MCP4261 *mcp_instance, uint8_t R1HW, uint8_t R1A, uint8_t R1W, uint8_t R1B, uint8_t R0HW, uint8_t R0A, uint8_t R0W, uint8_t R0B){
    uint8_t temp_value {0};
    temp_value = (R1HW << 0x7) | (R1A << 0x6) | (R1W << 0x5) | (R1B << 0x4) | 
                 (R0HW << 0x3) | (R0A << 0x2) | (R0W << 0x1) | (R0B << 0x0);
    if(mcp_instance->set_TCON(temp_value));
    else printf("TCON set FAIL \n");
    usleep(sleep_time);
}

void TEST_get_TCON(MCP4261 *mcp_instance, uint32_t sleep_time){
        uint8_t MCP_read_value {0};
        if (!mcp_instance->get_TCON(MCP_read_value)) printf("FAIL READ TCON!! \n");
        else printf("TCON read value is %x \n", MCP_read_value);
        usleep(sleep_time);
}

void TEST_set_WP_SHDN(MCP4261 *mcp_instance, bool is_active, mcp_gpio_t& pin, uint32_t sleep_time){
    if(mcp_instance->set_write_protect_shutdown(is_active, pin));
    else printf("TCON set FAIL \n");
    usleep(sleep_time);
}

void TEST_get_WP_SHDN(MCP4261 *mcp_instance, uint32_t sleep_time){
        uint32_t MCP_read_value {0};
        if (!mcp_instance->get_write_protect_shutdown(MCP_read_value)) printf("FAIL READ WP_SHDN!! \n");
        else printf("GPIO read value is %x \n", MCP_read_value);
        usleep(sleep_time);
}

void TEST_all_TCON(MCP4261 *mcp_instance, uint32_t sleep_time){
    uint8_t MCP_read_value {0};
    uint8_t mis_match_count {0};

    for (uint8_t i=0; i<0xff; i++){
        if(mcp_instance->set_TCON(i));
        else print("TCON set FAIL \n");
        usleep(sleep_time);

        if (!mcp_instance->get_TCON(MCP_read_value)) printf("FAIL READ TCON!! \n");
        else {
            if(MCP_read_value!=i) {
                mis_match_count++; 
                printf("TCON read value is %x \n", MCP_read_value);
            }
            else printf("TCON read value is %x \n", MCP_read_value);
            }
        usleep(sleep_time);
    }
    printf("mismatch time is: %d \n", mis_match_count);  
    
    //to make the TCON into default setting
    if(!mcp_instance->set_TCON(0xff)) print("TCON set FAIL \n");
}