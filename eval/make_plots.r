library(tidyverse)
library(scales)


pdf("plots.pdf", width = 10, height = 6)

data <- as_tibble(jsonlite::fromJSON(file("benchmark.json"))$benchmarks) %>%
    transmute(name = name, time = real_time) %>%
    separate_wider_delim(name, '/', names = c("name", "threads")) %>%
    mutate(name = fct_relabel(factor(name), ~ gsub("^BM_", "", .)), threads = as.numeric(threads)) %>%
    separate_wider_regex(name, c(type = "[A-Za-z_]+", "<", params = "[A-Za-z0-9>, ]+")) %>%
    separate_wider_regex(params, c(min = "[0-9]+", ", ", max = "[0-9]+", ", ", prefill = "[0-9]+", range_size_pre = ",? ?[0-9]*", ", ", op_p_th = "[0-9]+", ", ", rebuild = "[a-z]+", ">")) %>%
    mutate(op_p_th = as.numeric(op_p_th))
    
print(data)

data %>%
    dplyr::filter(type=="special") %>%
    ggplot(aes(x = log2(threads), y =  (op_p_th * threads * 1000000000)/time, color = rebuild, shape = rebuild)) +
           geom_line() +
           geom_point() +
           scale_x_continuous("Nr. of Threads", labels = math_format(2^.x)) +
           scale_y_continuous("Ops/s") +
           labs(title = "Comparison (insertions on all int's)")

data %>%
    dplyr::filter(type=="tree") %>%
    separate_wider_regex(range_size_pre, c(", ", range_size = "[0-9]*")) %>%
    dplyr::filter(range_size=="100") %>%
    ggplot(aes(x = log2(threads), y =  (op_p_th * threads * 1000000000)/time, color = paste("PF: ", prefill, "RB: ", rebuild, sep = " "), shape = paste("PF: ", prefill, "RB: ", rebuild, sep = " "))) +
           geom_line() +
           geom_point() +
           scale_x_continuous("Nr. of Threads", labels = math_format(2^.x)) +
           scale_y_continuous("Ops/s") +
           labs(title = "Comparison (all operations) with varying prefill")

data %>%
    dplyr::filter(type=="tree" & prefill=="50") %>%
    separate_wider_regex(range_size_pre, c(", ", range_size = "[0-9]*")) %>%
    ggplot(aes(x = log2(threads), y =  (op_p_th * threads * 1000000000)/time, color = paste("RS: ", range_size, "RB: ", rebuild, sep = " "), shape = paste("RS: ", range_size, "RB: ", rebuild, sep = " "))) +
           geom_line() +
           geom_point() +
           scale_x_continuous("Nr. of Threads", labels = math_format(2^.x)) +
           scale_y_continuous("Ops/s") +
           labs(title = "Comparison (all operations) with varying range size")

data %>%
    dplyr::filter(type=="lookup") %>%
    ggplot(aes(x = log2(threads), y =  (op_p_th * threads * 1000000000)/time, color = paste("PF: ", prefill, "RB: ", rebuild, sep = " "), shape = paste("PF: ", prefill, "RB: ", rebuild, sep = " "))) +
           geom_line() +
           geom_point() +
           scale_x_continuous("Nr. of Threads", labels = math_format(2^.x)) +
           scale_y_continuous("Ops/s") +
           labs(title = "Comparison (only lookup ops)")

data %>%
    dplyr::filter(type=="norange") %>%
    ggplot(aes(x = log2(threads), y =  (op_p_th * threads * 1000000000)/time, color = paste("PF: ", prefill, "RB: ", rebuild, sep = " "), shape = paste("PF: ", prefill, "RB: ", rebuild, sep = " "))) +
           geom_line() +
           geom_point() +
           scale_x_continuous("Nr. of Threads", labels = math_format(2^.x)) +
           scale_y_continuous("Ops/s") +
           labs(title = "Comparison (no range queries)")

dev.off()
