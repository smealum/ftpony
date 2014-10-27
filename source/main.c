#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <3ds.h>

#include "costable.h"
#include "text.h"
#include "ftp.h"

char superStr[8192];
int cnt;

char* quotes[]={"\"wow this is the worst thing i've seen in a while\"\n",
			"\"<Namidairo> that hurts my brain\"\n"};
const int numQuotes = 2;

s32 pcCos(u16 v)
{
	return costable[v&0x1FF];
}

void drawFrame()
{
	u8* bufAdr=gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);

	int i, j;
	for(i=1;i<400;i++)
	{
		for(j=1;j<240;j++)
		{
			u32 v=(j+i*240)*3;
			bufAdr[v]=(pcCos(i+cnt)+4096)/32;
			bufAdr[v+1]=(pcCos(j-256+cnt)+4096)/64;
			bufAdr[v+2]=(pcCos(i+128-cnt)+4096)/32;
		}
	}
	drawString(bufAdr, "ftPONY v0.0002\n", 0, 0);
	drawString(bufAdr, quotes[rand()%numQuotes], 0, 8);
	drawString(bufAdr, superStr, 16, 20);
	cnt++;

	gfxFlushBuffers();
	gfxSwapBuffers();
}

int main()
{
	srvInit();	
	aptInit();
	hidInit(NULL);
	irrstInit(NULL);
	gfxInit();

	aptSetupEventHandler();
	gfxSet3D(false);

	srand(svcGetSystemTick());

	superStr[0]=0;
	ftp_init();

	int connfd=ftp_getConnection();

	APP_STATUS status;
	while((status=aptGetStatus())!=APP_EXITING)
	{
		if(status == APP_RUNNING)
		{
			ftp_frame(connfd);
			drawFrame();

			hidScanInput();
			if(hidKeysDown()&KEY_B)break;
		}
		else if(status == APP_SUSPENDING)
		{
			aptReturnToMenu();
		}
		else if(status == APP_SLEEPMODE)
		{
			aptWaitStatusEvent();
		}
		gspWaitForEvent(GSPEVENT_VBlank0, false);
	}

	ftp_exit();
	gfxExit();
	irrstExit();
	hidExit();
	aptExit();
	srvExit();
	return 0;
}
