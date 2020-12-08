tracing Part II: subsection DEMO: "ebpf_exporter+prometheus+grafana"
================================================================================

Combining ebpf with prometheus and grafana to get pretty run-"queue"-(redblacktree)-graphs like this:

![runqlat](https://raw.githubusercontent.com/Stefan20162016/tracing/master/pics/runqlat_heatmap.png)

Setup: prometheus:

```
scrape_configs:
  - job_name: 'eBPF'
    scrape_interval: 5s
    static_configs:
      - targets:
        - "localhost:9435"  
  - job_name: 'eBPF_nr2'
    scrape_interval: 5s
    static_configs:
      - targets:
        - "localhost:9436"            
```
run ebpf_exporter:
```
./ebpf_exporter --config.file=examples/runqlat.yaml
./ebpf_exporter --config.file=examples/bio-tracepoints.yaml --web.listen-address=":9436"
```



Config of grafana dashboard: [click-config](https://raw.githubusercontent.com/Stefan20162016/tracing/master/pics/runqlat_edit_threads_6_20_40_80_200.png). Important to click on the right panel: "Data format": "time series buckets", heatmap .

In the picture you can also see runqueue-latencies of running the fs-scanner with 6, 20, 40, 80 and 200 threads on the right side of the graph. Clearly the latency icreases with too many threads. Hotspot is around 128 microsecs with less threads it's at the bottom at 2 microsecs.

full dashboard of block IO bytes and latency and run-queue-latency heatmaps:
![fulldash](https://raw.githubusercontent.com/Stefan20162016/tracing/master/pics/dashboard_bio_runqlat.png)

## Links:

1. <a name="ebpf_exporter">https://github.com/cloudflare/ebpf_exporter#programs</a>       (ebpf_exporter)
1. <a name="prometheus">https://prometheus.io/</a> description      (prometheus)
1. <a name="grafana">https://grafana.com/</a> description      (grafana)
1. <a name=""></a> description      ()
1. <a name=""></a> description      ()