// SPDX-License-Identifier: GPL-2.0

struct lkl_host_operations;
struct lkl_host_operations *lkl_ops;
extern struct lkl_host_operations lkl_host_ops;

int uml_main(int, char **, char **);
int main(int argc, char **argv, char **envp)
{
	lkl_ops = &lkl_host_ops;
	return uml_main(argc, argv, envp);
}
