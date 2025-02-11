#include <unistd.h>
#include <stdio.h>

#define __USE_POSIX199309
#include <time.h>

#define WF_STANDALONE
#include "wf.h"

Vector3 SomeGlobalObjectPosition;
int SomeGlobalIntValue, SomeOtherInt;

int main()
{

    int SomeConst = 250;

    Wf_FiddleWithVec3(&SomeGlobalObjectPosition, "SomeGlobalObjectPosition", (Vector3){0, 0, 0}, (Vector3){100, 100, 100});
    Wf_FiddleWithInt(&SomeGlobalIntValue, "SomeGlobalIntValue", 0, 100);
    Wf_FiddleWithInt(&SomeOtherInt, "SomeOtherInt", 0, 10);
    Wf_FiddleWithInt(&SomeConst, "some const", 0, 500);

    // JsonTest();
    // return 0;

    Wf_StartServer(8081);

    while (1)
    {

        // LOG("SERVER RUN");

        Wf_RunFiddle();

        SomeGlobalIntValue += 1;
        if (SomeGlobalIntValue > 99)
        {
            SomeGlobalIntValue = 0;
            SomeOtherInt += 1;

            if (SomeOtherInt > 99)
            {
                SomeOtherInt = 0;
            }
        }

        int sr = nanosleep(&(struct timespec){
                               .tv_sec = 0,
                               .tv_nsec = 1000 * 1000 * 75},
                           NULL);

        if (0 > sr)
        {
            perror("nanosleep");
            break;
        }
    }

    return 0;
}