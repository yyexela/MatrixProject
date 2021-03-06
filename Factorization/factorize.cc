#include <iostream>
#include <fstream>
#include "parse-csv.h"
#include "factorize.h"
#include "float.h"
#include <cmath>

using namespace std;

Parse parse_vars;
Factorize factorize_vars;

int main(int argc, char* argv[]){
    if(! (LOAD_SERIALIZED) ){
        ofstream wfs("time.log", ofstream::out | ofstream::trunc);
        wfs << "Features: " << FEATURES << endl;
        wfs << "Threshold: " << ERR_THRESH << endl;
        wfs << "Vector init: " << FEATURE_INIT << endl;
        wfs << "Epochs: " << EPOCHS << endl;
        wfs << endl;
        wfs << "Start time: " << clock() / (double) CLOCKS_PER_SEC << endl;
        wfs.close();
    }

    // Load in the files
    ProcessFiles();
    
    if(LOAD_SERIALIZED){
        cout << "Loading data from \"serialized\"" << endl;
        ifstream ifs("serialized");
        boost::archive::text_iarchive ia(ifs);
        ia >> factorize_vars;

        // Reset the other structures in factorize_vars
        ResErrInit();
        for(int i = 0; i < FEAT_DONE; ++i){
            UpdateMags(i);
            UpdateResErr(i);
        }


        PrintTimestamp();
        cout << endl;
    } else {
        // Initialize user/item feature vectors
        cout << "Initializing user/item feature vectors" << endl;
        FeatureInit();
        PrintTimestamp();
        cout << endl;

        cout << "Initializing res_err array of vectors to riu" << endl;
        ResErrInit();
        if(ResErrSize() != VArrSize()){
            cout << "ERROR: Mismatched VArrSize and ResArrSize" << endl;
            exit(1);
        }

        PrintTimestamp();
        cout << endl;
    }

    // Stochastic Gradient Descent
    cout << "Training the data" << endl << endl;
    Train();
    cout << "Done training the data" << endl;
    PrintTimestamp();
    cout << endl;

    cout << "Serializing the data to \"serialized\"" << endl;
    PrintTimestamp();
    cout << endl;

    // Serialize the data so it isn't lost
    {
            ofstream ofs("serialized");
            boost::archive::text_oarchive oa(ofs);
            oa << factorize_vars;
    }
    
    if(PREDICT_FINAL){
        for(int i = 0; i < ITEMS; i++){
            for(int j = 0; j < USERS; j++){
                double prediction = 0;
                for(int n = 0; n < FEATURES; n++){
                    prediction += factorize_vars.item_f[i][n] * factorize_vars.user_f[j][n];
                }
                cout << "prediction for item " << i+1 << " user " << j+1 << " is " << prediction << endl;
            }
        }
    }

    cout << "Program finished, exiting" << endl;
    PrintTimestamp();
}

/*
 * Loops through each existing rating and works
 * on updating a feature before moving onto the next
 */
void Train(){
    //We need to initialize total_err and old_err
    //to two different values
    double total_err;
    double total_reg_err;
    double old_reg_err;

    //Train one feature at a time
    for(int n = FEAT_DONE; n < FEATURES; n++){
        total_reg_err = DBL_MAX/1.125;
        old_reg_err = DBL_MAX;
        total_err = 0;
        cout << "Feature " << n+1 << endl << endl;

        //Run through EPOCHS steps per feature
        for(int step = 0; step < EPOCHS; step++){
            old_reg_err = total_reg_err;
            total_reg_err = 0.0;
            // 1-step gradient descent
            // loop through all existing riu once
            for(int i = 0; i < ITEMS; i++){
                for(unsigned int j = 0; j < parse_vars.items_v[i].size(); j++){
                    unsigned int item = parse_vars.items_v[i].at(j).item;
                    unsigned int uid = parse_vars.items_v[i].at(j).uid;

                    double curr_res_err = factorize_vars.res_err[i].at(j); // The cached residual error

                    double item_feat = factorize_vars.item_f[item-1][n]; // *_feat is the value of the current feature
                    double user_feat = factorize_vars.user_f[uid-1][n];
                    double product_feat = item_feat * user_feat;

                    // clipping: seems to increase run time by a SIGNIFICANT amount
                    /*
                    product_feat = product_feat > 5 ? 5 : product_feat;
                    product_feat = product_feat < 0 ? 0 : product_feat;
                    */

                    double err = curr_res_err - (product_feat);

                    double item_mag = factorize_vars.mag_item[item-1]; // *_mag is the value of the magnitude of features 1-(n-1) 
                    double user_mag = factorize_vars.mag_user[uid-1];

                    total_reg_err += (pow(err,2) + K * ((item_mag + pow(item_feat,2)) + user_mag + pow(user_feat,2))); // regularized square error
                    
                    factorize_vars.user_f[uid-1][n] += LRATE * (err * item_feat - K * user_feat);
                    factorize_vars.item_f[item-1][n] += LRATE * (err * user_feat - K * item_feat);
                }
            }
            if(PRINT_STEP){
                cout << "Regularized Total Error: " << total_reg_err << endl;
                cout << "Regularized Old Error: " << old_reg_err << endl;
                cout << "Difference: " << old_reg_err - total_reg_err << endl;
                cout << "Step: " << step+1 << endl;
                PrintTimestamp();
                cout << endl;
            }
        }
        
        UpdateResErr(n);
        UpdateMags(n);

        // Update total error
        total_err = 0;
        for(int i = 0; i < ITEMS; i++){
            for(unsigned int j = 0; j < parse_vars.items_v[i].size(); j++){
                double curr_res_err = factorize_vars.res_err[i].at(j); // The updated cached residual error

                total_err += pow(curr_res_err,2); 
            }
        }

        cout << "Trained feature " << n+1 << endl;

        ofstream wfs("time.log", ofstream::out | ofstream::app);
        wfs << "Trained feature " << (n+1) << ". Elapsed time: " << (clock() - parse_vars.start) / (double) CLOCKS_PER_SEC << endl;
        wfs << "Total error: " << total_err << endl;
        wfs << "Total regularized error: " << total_reg_err << endl;
        if(old_reg_err <= total_reg_err){
            wfs << "Couldn't minimize difference more than " << abs(old_reg_err - total_reg_err) << endl;
        }
        wfs << endl;
        wfs.close();

        PrintTimestamp();
        cout << endl;

        if(SAVE_PER_FEAT){
            cout << "Saving item_f and user_f" << endl;
            {
                string file = "serialized";
                ofstream ofs(file);
                boost::archive::text_oarchive oa(ofs);
                oa << factorize_vars;
            }
            PrintTimestamp();
            cout << endl;
        }
    }
}

/*
 * Updates the magnitude squared of each
 * user/item vector for feature n
 */
void UpdateMags(int n){
    for(int j = 0; j < ITEMS; j++){
        double feat = factorize_vars.item_f[j][n];
        factorize_vars.mag_item[j] += pow(feat,2);
    }
    for(int j = 0; j < USERS; j++){
        double feat = factorize_vars.user_f[j][n];
        factorize_vars.mag_user[j] += pow(feat,2);
    }
}

/*
 * Initialize user/item feature matrices
 * to defined global value in globals.h
 * as FEATURE_INIT
 */
void FeatureInit(){
    // Initialize user-feature
    for(int i = 0; i < USERS; ++i){
        for(int j = 0; j < FEATURES; ++j){
            factorize_vars.user_f[i][j] = FEATURE_INIT;
        }
    }

    // Initialize item-feature
    for(int i = 0; i < ITEMS; ++i){
        for(int j = 0; j < FEATURES; ++j){
            factorize_vars.item_f[i][j] = FEATURE_INIT;
        }
    }
}

/*
 * Update the res_err vector with the n-th
 * feature is put into each user/item-pair's residual error
 */
void UpdateResErr(short n){
    for(int i = 0; i < ITEMS; i++){
        for(unsigned int j = 0; j < factorize_vars.res_err[i].size(); j++){
            unsigned int item = parse_vars.items_v[i].at(j).item;
            unsigned int uid = parse_vars.items_v[i].at(j).uid;
            double item_feat = factorize_vars.item_f[item-1][n];
            double user_feat = factorize_vars.user_f[uid-1][n];
            double residual = (item_feat * user_feat);
            double old_rating = factorize_vars.res_err[i].at(j);
            double new_rating = old_rating - residual;
            factorize_vars.res_err[i].at(j) = new_rating;
        }
    }
}

/*
 * Sets res_err to riu for each item/user
 * Same functionality as UpdateResErr, but instead of
 * updating it initializes the array of vectors
 */
void ResErrInit(){
    for(int i = 0; i < ITEMS; i++){
        for(unsigned int j = 0; j < parse_vars.items_v[i].size(); j++){
            double rating = parse_vars.items_v[i].at(j).rating;
            factorize_vars.res_err[i].push_back(rating);
        }
    }
}

/*
 * Used to check to make sure the size
 * of res_err is correct
 */ 
unsigned int ResErrSize(){
    unsigned int size = 0;
    for(int i = 0; i < ITEMS; i++){
        size += factorize_vars.res_err[i].size();
    }
    return size;
}
