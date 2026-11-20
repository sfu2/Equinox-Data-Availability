/* Standalone CSA smoke test: intentionally releases one reference twice.
 * The API declarations match the checker's built-in fget/fput model.
 * No kernel headers or API implementations are needed for static analysis.
 */
struct file;
extern struct file *fget(unsigned int fd);
extern void fput(struct file *file);

void refcount_smoke(void) {
  struct file *p = fget(0);
  if (!p)
    return;
  fput(p);
  fput(p); /* Expected: Reference count is negative, source: fget, dec: ... */
}
