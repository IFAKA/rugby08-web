/* plain_calls.c -- the rig measures the original's behaviour: a function that another branch overrides in
 * the shared gen/ (lifted as sub_X_lifted) is called as lifted here, unchanged. */
void sub_0079B230_lifted(void);
void sub_0079B230(void) { sub_0079B230_lifted(); }
