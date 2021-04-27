#include <iostream>
//#include "SatSolver.h"
//#include "Cats.h"
#include "ErrorManager.h"
//#include "Circuit.h"
#include "Dataflow.h"
#include "MILP_Model.h"
//#include "Dataflow.h"
#include "DFnetlist.h"
#include <sstream>


using namespace std;

using namespace Dataflow;

using vecParams = vector<string>;

string exec;
string command;



int main_buffers(const vecParams& params)
{
    if (params.size() != 5) {
        cerr << "Usage: " + exec + ' ' + command + " period buffer_delay solver infile outfile" << endl;
        return 1;
    }

    DFnetlist DF(params[3]);

    if (DF.hasError()) {
        cerr << DF.getError() << endl;
        return 1;
    }

    // Lana 07.03.19. Removed graph optimizations (temporary)
    //DF.optimize();
    double period = atof(params[0].c_str());
    double delay = atof(params[1].c_str());

    cout << "Adding elastic buffers with period=" << period << " and buffer_delay=" << delay << endl;
    DF.setMilpSolver(params[2]);

    bool stat = DF.addElasticBuffers(period, delay, true);

    // Lana 05/07/19 Instantiate if previous step successful, otherwise just print and exit
    if (stat) {
        DF.instantiateElasticBuffers();
    }
    //DF.instantiateElasticBuffers();
    DF.writeDot(params[4]);
    return 0;
}


int main_help()
{
    cerr << "Usage: " + exec + " cmd params" << endl;
    cerr << "Available commands:" << endl;
    cerr << "  dataflow:      handling dataflow netlists." << endl;
    cerr << "  buffers:       add elastic buffers to a netlist." << endl;
    cerr << "  async_synth:   synthesize an asynchronous circuit." << endl;
    cerr << "  solveCSC:      solve state encoding in an asynchronous circuit." << endl;
    cerr << "  hideSignals:   hide signals in an asynchronous specification." << endl;
    cerr << "  syncprod:      calculate the synchronous product of two LTSs." << endl;
    cerr << "  bisimilar:     check whether two LTSs are weakly bisimilar." << endl;
    cerr << "  bbg:           unit test for Basic Block graphs." << endl;
    cerr << "  circuit:       unit test to read and write a circuit." << endl;
}

<<<<<<< HEAD
int main_shab(const vecParams& params){
=======
#include <regex>

struct user_input {
    string graph_name;
    string solver;
    double period;
    double delay;
    double first;
    int timeout;
    bool set;
};

void clear_input(user_input& input) {
    input.graph_name = "dataflow";
    input.set = true;
    input.first = false;
    input.delay = 0.0;
    input.period = 5;
    input.timeout = 180;
    input.solver = "cbc";
}
>>>>>>> b1f302209bb6b2185f0525cc41fb2da1d1773a5b


    if (params.size() != 9 && params.size() != 10) {
        cerr << "Usage: " + exec + ' ' + command + " period buffer_delay solver infile outfile" << endl;
        return 1;
    }


    if (params.size() == 9) {
        DFnetlist DF(params[4], params[5]);

        if (DF.hasError()) {
            cerr << DF.getError() << endl;
            return 1;
        }
        double period = atof(params[0].c_str());
        double delay = atof(params[1].c_str());
        cout << "Adding elastic buffers with period=" << period << " and buffer_delay=" << delay << endl;
        cout << endl;
        DF.setMilpSolver(params[2]);

        bool stat;
        if (stoi(params[3]))
            stat = DF.addElasticBuffersBB_sc(period, delay, true, 1, -1, stoi(params[8]));
        else
            stat = DF.addElasticBuffersBB(period, delay, true, 1, -1, stoi(params[8]));

        if (stat) {
            DF.instantiateElasticBuffers();
        }
        DF.writeDot(params[6]);
        DF.writeDotBB(params[7]);

    } else {
        DFnetlist DF(params[5], params[6]);
        if (DF.hasError()) {
            cerr << DF.getError() << endl;
            return 1;
        }
        double period = atof(params[0].c_str());
        double delay = atof(params[1].c_str());
        cout << "Adding elastic buffers with period=" << period << " and buffer_delay=" << delay << endl;
        cout << endl;
        DF.setMilpSolver(params[2]);

        bool stat;
        if (stoi(params[3]))
            stat = DF.addElasticBuffersBB_sc(period, delay, true, 1, stoi(params[4]), stoi(params[9]));
        else
            stat = DF.addElasticBuffersBB(period, delay, true, 1, stoi(params[4]), stoi(params[9]));

        if (stat) {
            DF.instantiateElasticBuffers();
        }
        DF.writeDot(params[7]);
        DF.writeDotBB(params[8]);
    }

    return 0;
}

int main_test(const vecParams& params){
    DFnetlist DF(params[0], params[1]);
//    DF.computeSCC(false);
  //  DF.printBlockSCCs();
}

int main(int argc, char *argv[])
{
    //return main_persistence(argc, argv);
    //return main_csv(argc, argv);
    //return main_nodal(argc, argv);
    //return main_windows(argc, argv);

    exec = basename(argv[0]);
    if (argc == 1) return main_help();

    command = argv[1];
    if (command == "help") return main_help();

    vecParams params;
    for (int i = 2; i < argc; ++i) params.push_back(argv[i]);

    if (command == "buffers") return main_buffers(params);
    if (command == "shab") return main_shab(params);
    if (command == "test") return main_test(params);

#if 0    
    if (command == "dataflow") return main_dataflow(params);
    if (command == "dflib") return main_dflib(params);
    if (command == "syncprod") return main_syncprod(params);
    if (command == "bisimilar") return main_bisimilar(params);
    if (command == "hideSignals") return main_hideSignals(params);
    if (command == "solveCSC") return main_solveCSC(params);
    if (command == "async_synth") return main_async_synth(params);
    if (command == "milp") return main_milp(params);
    if (command == "bbg") return main_bbg();
    if (command == "circuit") return main_circuit(params);
#endif
    cerr << command << ": Unknown command." << endl;
    return main_help();
}
