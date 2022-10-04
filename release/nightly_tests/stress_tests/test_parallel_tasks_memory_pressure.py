from math import ceil
import ray
import time
import psutil
import multiprocessing


@ray.remote
def allocate_memory(
    total_allocate_bytes: int,
    num_chunks: int = 10,
    allocate_interval_s: float = 0,
):
    chunks = []
    # divide by 8 as each element in the array occupies 8 bytes
    bytes_per_chunk = total_allocate_bytes / 8 / num_chunks
    for _ in range(num_chunks):
        chunks.append([0] * ceil(bytes_per_chunk))
        time.sleep(allocate_interval_s)


def get_additional_bytes_to_reach_memory_usage_pct(pct: float) -> int:
    node_mem = psutil.virtual_memory()
    used = node_mem.total - node_mem.available
    bytes_needed = node_mem.total * pct - used
    assert bytes_needed > 0, "node has less memory than what is requested"
    return int(bytes_needed)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--num-tasks",
        help="number of tasks to process in total",
        default="100",
        type=int,
    )

    parser.add_argument(
        "--mem-pct-per-task",
        help="memory to allocate per task as a fraction of the node's available memory",
        default="0.4",
        type=float,
    )

    parser.add_argument(
        "--parallelism",
        help="parallelize tasks by adjusting num_cpu per task",
        default="16",
        type=int,
    )

    args = parser.parse_args()

    cpu_count = multiprocessing.cpu_count()
    cpu_per_task = ceil(cpu_count / args.parallelism)

    bytes_per_task = get_additional_bytes_to_reach_memory_usage_pct(
        args.mem_pct_per_task
    )

    start = time.time()
    task_refs = [
        allocate_memory.options(num_cpus=cpu_per_task).remote(
            total_allocate_bytes=bytes_per_task, allocate_interval_s=1
        )
        for _ in range(args.num_tasks)
    ]
    ray.get(task_refs)
    end = time.time()

    print(f"processed {args.num_tasks} tasks in {end-start} seconds")
