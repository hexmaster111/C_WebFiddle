#ifndef WF_H
#define WF_H

#ifdef WF_STANDALONE
typedef struct Vector3
{
    float x, y, z;
} Vector3;

#define LOG(MSG) printf("WF: " MSG "\n")
#endif // WF_STANDALONE

/* start webserver on port */
void Wf_StartServer(int port);

/* run the web server */
void Wf_RunFiddle();

/* Add a Vector3 to fiddle with */
void Wf_FiddleWithVec3(Vector3 *v, const char *lbl, Vector3 min, Vector3 max);

/* Add an int to fiddle with */
void Wf_FiddleWithInt(int *v, const char *lbl, int min, int max);

void JsonTest();

#endif // WF_H