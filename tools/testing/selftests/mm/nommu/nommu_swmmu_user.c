uint64_t nommu_swmmu_load_u64(const void *address,
			       size_t size)
{
	return syscall(SYS_nommu_swmmu_load,
		       (uintptr_t)address,
		       size,
		       0);
}

void nommu_swmmu_store_u64(void *address,
			   size_t size,
			   uint64_t value)
{
	syscall(SYS_nommu_swmmu_store,
		(uintptr_t)address,
		size,
		value);
}

void *nommu_swmmu_alloc(size_t size)
{
	long ret;

	ret = syscall(SYS_nommu_swmmu_alloc, size);
	if (ret < 0)
		return NULL;

	return (void *)(uintptr_t)ret;
}
