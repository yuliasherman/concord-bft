import pandas as pd
import glob
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import FormatStrFormatter


# fs=12
def csvs2df(files):
    list_ = []
    for file_ in files:
        df = pd.read_csv(file_, index_col=None, header=0)
        list_.append(df)

    frame = pd.concat(list_, axis=0, ignore_index=True)
    return frame


def get_points(data):
    points = []
    for s in data.items():
        for el in s[1].items():
            points += [el]  # * int(el[1])
    return points


def plot_latency_for_tps(dataFolder, out_path):
    fig, ax1 = plt.subplots(nrows=1, ncols=1)
    datafile = glob.glob(dataFolder + "/**/results1.csv")
    tx_values, x_t = get_avg_tps_per_client_num(datafile)
    latencies_ = get_avg_latency_per_client_num(glob.glob(dataFolder + "/**/results.csv"))
    ax1.plot(tx_values, latencies_)
    # plt.xticks(np.arange(0, 3001, step=500))
    # plt.yticks(np.arange(0, 61, step=10))
    plt.savefig(out_path + '/latAsTps.pdf')
    plt.savefig(out_path + '/latAsTps')


def plot_tps_and_latency(dataFolder, out_path):
    fs = 12
    datafile = glob.glob(dataFolder + "/**/results1.csv")
    tx_values, x_t = get_avg_tps_per_client_num(datafile)
    latencies_ = get_avg_latency_per_client_num(glob.glob(dataFolder + "/**/results.csv"))
    # plt.plot(tx_values, latencies_)
    fig, ax1 = plt.subplots(nrows=1, ncols=1)
    color = 'tab:red'
    ax1.set_xlabel('Clients')
    ax1.set_ylabel('TPS ($\\frac{transactions}{sec}$)', color=color)
    ax1.plot(x_t, tx_values, color=color)
    ax1.tick_params(axis='y', labelcolor=color)
    plt.xticks(np.arange(0, 201, step=20))
    # ax1.yticks(np.arange(0, 3001, step=250))

    ax2 = ax1.twinx()
    color = 'tab:blue'
    ax2.set_ylabel('Latency (ms)', color=color)
    ax2.plot(x_t, latencies_, color=color)
    ax2.tick_params(axis='y', labelcolor=color)
    # ax1.plot(data_['clients'], tx_values)
    plt.savefig(out_path + '/tpsAndLat.pdf')
    plt.savefig(out_path + '/tpsAndLat')


def get_avg_latency_per_client_num(files):
    latencies = collect_latencies(files)
    return latencies.groupby(latencies.clients).mean()


def get_avg_tps_per_client_num(files):
    all = pd.DataFrame(columns=['clients', 'size', 'time'], dtype=float)
    for f in files:
        data = csvs2df([f])
        data_ = data[['clients', 'size', 'time']]
        data_ = data_.groupby(data.clients).agg({'clients': 'mean', 'size': 'sum', 'time': 'max'})
        # tx_values = (data_['size'] / (data_['time'] / 1000))
        all = all.append(data_)
    data_ = all[['clients', 'size', 'time']]
    data_ = data_.groupby(all.clients).mean()
    return data_['size'] / (data_['time'] / 1000), data_['clients']


def collect_latencies(files):
    data = csvs2df(files)
    data_ = data.groupby(data.clients).sum()
    res = pd.DataFrame(columns=['clients', 'latency'], dtype=int)
    for item in data_.items():
        for tup in item[1].items():
            ser = pd.DataFrame({'clients': tup[0], 'latency': int(float(item[0]))}, index=range(int(tup[1])))
            if ser.size == 0:
                continue
            res = res.append(ser)
    return res


def plot_boxes(files_for_ordinary, files_for_pex, out_path):
    latencies_ = collect_latencies(glob.glob(files_for_ordinary + "/**/results.csv"))
    fig, ax = plt.subplots(nrows=1, ncols=1)  # ,  figsize=(4.5, 3.5))
    data = []
    data += [latencies_['latency'] / 1000]
    if files_for_pex != "":
        latencies_ = collect_latencies(glob.glob(files_for_pex + "/**/results.csv"))
        data += [latencies_['latency'] / 1000]
    else:
        data += [[]]
    plt.boxplot(data, 0, '', whis=[0, 100])
    ax.set_xticklabels(('npex', 'pex'))
    plt.savefig(out_path + '/boxes.pdf')
    plt.savefig(out_path + '/boxes')


def plot_histograms(files_for_ordinary, files_for_late, files_for_pex, out_path):
    latencies_ = collect_latencies(glob.glob(files_for_ordinary + "/**/results.csv"))
    fig, ax = plt.subplots(nrows=1, ncols=3, sharey=True)
    latencies__ = np.array([])
    if files_for_late != "":
        latencies__ = collect_latencies(glob.glob(files_for_late + "/**/results.csv"))
    latencies_pex = np.array([])
    if files_for_late != "":
        latencies_pex = collect_latencies(glob.glob(files_for_pex + "/**/results.csv"))
    ax[0].hist([latencies_['latency']], bins=6)

    ax[0].grid(True)
    ax[0].set_title("short jobs only\n(without pre-execution)", fontsize=10)

    ax[1].hist([latencies__['latency']], bins=6)
    ax[1].grid(True)
    ax[1].set_title("25% long jobs\n(without pre-execution)", fontsize=10)

    ax[2].hist([latencies_pex['latency']], bins=6)
    ax[2].grid(True)
    ax[2].set_title("25% long jobs\n(with pre-execution)", fontsize=10)
    # plt.sca(ax[2])
    # plt.xticks(range(0, 10, 1))
    print("shorts size:", len(latencies_['latency']))
    print("longs size:", len(latencies__['latency']))
    print("longs_pex size:", len(latencies_pex['latency']))
    fig.text(0.53, 0.02, "latency (ms)", ha="center", va="center")
    fig.tight_layout(rect=[0, 0.01, 1, 0.96])
    plt.savefig(out_path + '/hist.pdf')
    plt.savefig(out_path + '/hist')

def plot_tps(files_for_ordinary, files_for_pex, out_path):
    tx_values, x_t = get_avg_tps_per_client_num(glob.glob(files_for_ordinary + "/**/results1.csv"))
    fig, ax = plt.subplots(nrows=1, ncols=1)  # ,  figsize=(4.5, 3.5))
    plt.plot(x_t, tx_values, "-s", markerfacecolor='none', markersize=5, linewidth=2,  color="tab:red")
    plt.savefig(out_path + '/tps.pdf')
    plt.savefig(out_path + '/tps')

if __name__ == "__main__":
    # plot_boxes("/tmp/concordbft/results/1250_20", "/tmp/concordbft/results/100_20", "/tmp/concordbft/results")
    plot_histograms("/tmp/concordbft/results/200_8", "/tmp/concordbft/results/201_8", "/tmp/concordbft/results/200_8", "/tmp/concordbft/results")
    # plot_tps("/tmp/concordbft/results/1000_21", "/tmp/concordbft/results/1250_20", "/tmp/concordbft/results")

    # plot_tps_and_latency("/tmp/concordbft/results/1000_101", "/tmp/concordbft/results")
    # plot_latency_for_tps("/tmp/concordbft/results/1000_101", "/tmp/concordbft/results")
