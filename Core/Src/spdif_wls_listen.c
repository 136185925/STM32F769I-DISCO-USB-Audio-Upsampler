#include "spdif_wls_listen.h"

#include <stddef.h>
#include <string.h>

#define SPDIF_WLS_LISTEN_INPUT_GAIN_44K1 3.56508231f
#define SPDIF_WLS_LISTEN_INPUT_GAIN_48K  3.56499982f
#define SPDIF_WLS_LISTEN_DITHER_LEFT_SEED  0x9E3779B9U
#define SPDIF_WLS_LISTEN_DITHER_RIGHT_SEED 0x243F6A88U

/* Port of garyaudioconverter's WLS LISTEN strategy, scaled to a two-stage
 * 2x + 2x implementation for the STM32F769:
 * - stage 1 uses audible-band constrained continuous-frequency WLS;
 * - 44.1 kHz: 20.5..22.05 kHz transition, passband weight 1;
 * - 48 kHz: 22..24 kHz transition, passband weight 16;
 * - transition weight is 0.1x passband and stopband weight is 100000;
 * - stage 2 is a short WLS image cleaner with stopband weight 100;
 * - both stages use 32x homomorphic minimum-phase factorization.
 * The causal stage-1 tails are then trimmed to 352/264 taps and compensated
 * to retain exactly -1.0 dB DC headroom in this medium-high-load profile.
 * Coefficients are stored phase-major as h[p + 2k]. */
static const float spdif_wls_listen_stage1_176k4[2U * 176U] =
{
  5.4316833848e-04f, 2.5410057977e-02f, 1.7720411718e-01f, 3.4113961458e-01f,
  4.6932503581e-02f, -1.7797176540e-01f, 1.2331741303e-01f, -2.7313157916e-02f,
  -4.5421417803e-02f, 8.1237956882e-02f, -8.6682938039e-02f, 7.2962991893e-02f,
  -4.9975607544e-02f, 2.4756055325e-02f, -1.6236772062e-03f, -1.7179131508e-02f,
  3.0825613067e-02f, -3.9369873703e-02f, 4.3359994888e-02f, -4.3577279896e-02f,
  4.0870618075e-02f, -3.6057900637e-02f, 2.9872549698e-02f, -2.2939162329e-02f,
  1.5767242759e-02f, -8.7555181235e-03f, 2.2019806784e-03f, 3.6834303755e-03f,
  -8.7654348463e-03f, 1.2970389798e-02f, -1.6274278983e-02f, 1.8691966310e-02f,
  -2.0267834887e-02f, 2.1067753434e-02f, -2.1172326058e-02f, 2.0671240985e-02f,
  -1.9658679143e-02f, 1.8229572102e-02f, -1.6476657242e-02f, 1.4488168992e-02f,
  -1.2346130796e-02f, 1.0125099681e-02f, -7.8913494945e-03f, 5.7023842819e-03f,
  -3.6067618057e-03f, 1.6441434855e-03f, 1.5443489247e-04f, -1.7661415040e-03f,
  3.1757836696e-03f, -4.3751574121e-03f, 5.3623067215e-03f, -6.1407508329e-03f,
  6.7186728120e-03f, -7.1081114002e-03f, 7.3241502978e-03f, -7.3841642588e-03f,
  7.3070619255e-03f, -7.1126339026e-03f, 6.8209092133e-03f, -6.4516286366e-03f,
  6.0237566940e-03f, -5.5550928228e-03f, 5.0619533285e-03f, -4.5589306392e-03f,
  4.0587228723e-03f, -3.5720546730e-03f, 3.1076348387e-03f, -2.6721982285e-03f,
  2.2705863230e-03f, -1.9058788894e-03f, 1.5795492800e-03f, -1.2916661799e-03f,
  1.0410971008e-03f, -8.2572869724e-04f, 6.4268358983e-04f, -4.8854935449e-04f,
  3.5957459477e-04f, -2.5186309358e-04f, 1.6154274635e-04f, -8.4916071501e-05f,
  1.8573582565e-05f, 4.0507100493e-05f, -9.4900242402e-05f, 1.4668388758e-04f,
  -1.9743590383e-04f, 2.4823003332e-04f, -2.9967431328e-04f, 3.5194400698e-04f,
  -4.0484650526e-04f, 4.5788148418e-04f, -5.1032070769e-04f, 5.6127400603e-04f,
  -6.0976704117e-04f, 6.5480696503e-04f, -6.9545133738e-04f, 7.3085632175e-04f,
  -7.6032680226e-04f, 7.8334403224e-04f, -7.9959334107e-04f, 8.0897216685e-04f,
  -8.1159500405e-04f, 8.0777605763e-04f, -7.9801602988e-04f, 7.8297290020e-04f,
  -7.6343485853e-04f, 7.4027827941e-04f, -7.1443786146e-04f, 6.8686157465e-04f,
  -6.5847730730e-04f, 6.3015456544e-04f, -6.0268281959e-04f, 5.7673576521e-04f,
  -5.5286067072e-04f, 5.3145620041e-04f, -5.1277677994e-04f, 4.9691810273e-04f,
  -4.8383639660e-04f, 4.7334964620e-04f, -4.6515630675e-04f, 4.5885256259e-04f,
  -4.5395825873e-04f, 4.4993750635e-04f, -4.4622606947e-04f, 4.4225301826e-04f,
  -4.3746983283e-04f, 4.3136411114e-04f, -4.2348948773e-04f, 4.1347689694e-04f,
  -4.0067991358e-04f, 3.9815617492e-04f, -3.5018159542e-04f, 3.1788981869e-04f,
  -3.1195397605e-04f, 3.0769719160e-04f, -2.9509616434e-04f, 2.7255734312e-04f,
  -2.4174484133e-04f, 2.0519971440e-04f, -1.6539495846e-04f, 1.2441206491e-04f,
  -8.3885090135e-05f, 4.5020457037e-05f, -8.6749323600e-06f, -2.4589769964e-05f,
  5.4437277868e-05f, -8.0713783973e-05f, 1.0339575238e-04f, -1.2256382615e-04f,
  1.3837208098e-04f, -1.5102906036e-04f, 1.6077524924e-04f, -1.6787515779e-04f,
  1.7260063032e-04f, -1.7522559210e-04f, 1.7601771106e-04f, -1.7523771385e-04f,
  1.7312937416e-04f, -1.6992032761e-04f, 1.6581753152e-04f, -1.6101064102e-04f,
  1.5566717775e-04f, -1.4993660443e-04f, 1.4394546452e-04f, -1.3780286827e-04f,
  1.3159972150e-04f, -1.2541335309e-04f, 1.1930253822e-04f, -1.1331446149e-04f,
  1.0748227942e-04f, -1.0183281847e-04f, 9.6380172181e-05f, -9.1135327239e-05f,
  8.6098771135e-05f, -8.1270947703e-05f, 7.6647069363e-05f, -7.2221111623e-05f,
  5.2710152231e-03f, 7.9497002065e-02f, 2.8992715478e-01f, 2.5539022684e-01f,
  -1.4881390333e-01f, -3.1269285828e-02f, 1.1587435752e-01f, -1.1732960492e-01f,
  7.7979803085e-02f, -2.9079977423e-02f, -1.2955755927e-02f, 4.2002253234e-02f,
  -5.7622037828e-02f, 6.1971299350e-02f, -5.8031573892e-02f, 4.8716276884e-02f,
  -3.6487370729e-02f, 2.3244960234e-02f, -1.0349804536e-02f, -1.2986501679e-03f,
  1.1169047095e-02f, -1.9008962438e-02f, 2.4767840281e-02f, -2.8534162790e-02f,
  3.0486248434e-02f, -3.0854919925e-02f, 2.9895972461e-02f, -2.7870446444e-02f,
  2.5030987337e-02f, -2.1612804383e-02f, 1.7828123644e-02f, -1.3863171451e-02f,
  9.8770279437e-03f, -6.0017686337e-03f, 2.3435400799e-03f, 1.0157823563e-03f,
  -4.0165381506e-03f, 6.6190171055e-03f, -8.8010877371e-03f, 1.0555826128e-02f,
  -1.1889182962e-02f, 1.2817757204e-02f, -1.3366682455e-02f, 1.3567673974e-02f,
  -1.3457220979e-02f, 1.3074966148e-02f, -1.2462238781e-02f, 1.1660789140e-02f,
  -1.0711665265e-02f, 9.6542946994e-03f, -8.5257049650e-03f, 7.3599228635e-03f,
  -6.1875083484e-03f, 5.0352537073e-03f, -3.9259791374e-03f, 2.8784913011e-03f,
  -1.9076007884e-03f, 1.0242833523e-03f, -2.3587013129e-04f, -4.5365202823e-04f,
  1.0433202842e-03f, -1.5348242596e-03f, 1.9321058644e-03f, -2.2409553640e-03f,
  2.4686125107e-03f, -2.6233566459e-03f, 2.7141410392e-03f, -2.7502398007e-03f,
  2.7409337927e-03f, -2.6952316985e-03f, 2.6216462720e-03f, -2.5279973634e-03f,
  2.4212724529e-03f, -2.3075244389e-03f, 2.1918267012e-03f, -2.0782423671e-03f,
  1.9698508549e-03f, -1.8687963020e-03f, 1.7763654469e-03f, -1.6930763377e-03f,
  1.6187963774e-03f, -1.5528569929e-03f, 1.4941842528e-03f, -1.4414123725e-03f,
  1.3930129353e-03f, -1.3473937288e-03f, 1.3030065456e-03f, -1.2584208744e-03f,
  1.2123974739e-03f, -1.1639320292e-03f, 1.1122971773e-03f, -1.0570522863e-03f,
  9.9804881029e-04f, -9.3541166279e-04f, 8.6952192942e-04f, -8.0097536556e-04f,
  7.3054665700e-04f, -6.5913528670e-04f, 5.8772042394e-04f, -5.1730737323e-04f,
  4.4888650882e-04f, -3.8338222657e-04f, 3.2161836862e-04f, -2.6428306592e-04f,
  2.1191015549e-04f, -1.6485626111e-04f, 1.2330074969e-04f, -8.7241482106e-05f,
  5.6505599787e-05f, -3.0758197681e-05f, 9.5328359748e-06f, 7.7530430644e-06f,
  -2.1761325115e-05f, 3.3213418646e-05f, -4.2845964344e-05f, 5.1395621995e-05f,
  -5.9558293287e-05f, 6.7969842348e-05f, -7.7182667155e-05f, 8.7651111244e-05f,
  -9.9715281976e-05f, 1.1359638302e-04f, -1.2939141016e-04f, 1.4707975788e-04f,
  -1.6652286286e-04f, 1.8748406728e-04f, -2.0963071438e-04f, 2.3251865059e-04f,
  -2.5224799174e-04f, 3.0056360993e-04f, -3.0701223295e-04f, 2.9945804272e-04f,
  -3.0895348755e-04f, 3.3140875166e-04f, -3.5862700315e-04f, 3.8463113015e-04f,
  -4.0598071064e-04f, 4.2106377077e-04f, -4.2942329310e-04f, 4.3128672405e-04f,
  -4.2724676314e-04f, 4.1808348033e-04f, -4.0463372716e-04f, 3.8773223059e-04f,
  -3.6816328065e-04f, 3.4664868144e-04f, -3.2382775680e-04f, 3.0025994056e-04f,
  -2.7642326313e-04f, 2.5272218045e-04f, -2.2948716651e-04f, 2.0698666049e-04f,
  -1.8542843463e-04f, 1.6496816534e-04f, -1.4571295469e-04f, 1.2773348135e-04f,
  -1.1106453167e-04f, 9.5713039627e-05f, -8.1659534771e-05f, 6.8867499067e-05f,
  -5.7283581555e-05f, 4.6846038458e-05f, -3.7482557673e-05f, 2.9116332371e-05f,
  -2.1666684916e-05f, 1.5056415577e-05f, -9.2070913524e-06f, 4.0449081098e-06f,
  5.0306817911e-07f, -4.5007859626e-06f, 8.0107784015e-06f, -1.1085550796e-05f,
  1.3776744709e-05f, -1.6127030904e-05f, 1.8174647266e-05f, -1.9950884962e-05f,
};

static const float spdif_wls_listen_stage2_176k4[2U * 16U] =
{
  4.2095731944e-02f, 4.3923145533e-01f, 5.8482125401e-02f, -5.4887566715e-02f,
  1.2911451049e-02f, 7.5509711169e-03f, -6.9928122684e-03f, 3.1540435157e-04f,
  2.6130806655e-03f, -1.6170867020e-03f, 1.3488983677e-04f, 2.7231153217e-04f,
  -7.7422948380e-05f, -4.7667675972e-05f, 5.7282454691e-06f, 1.2263923281e-06f,
  2.1503596008e-01f, 4.0140292048e-01f, -1.5925273299e-01f, 5.5385243148e-02f,
  -1.4097525738e-02f, 9.2803100415e-05f, 2.7309504803e-03f, -1.5891739167e-03f,
  -1.4230849047e-04f, 7.9693587031e-04f, -3.9473397192e-04f, -5.2370385674e-05f,
  8.7817104941e-05f, 9.4174138212e-06f, -4.1053745008e-06f, -9.0175319656e-07f,
};

static const float spdif_wls_listen_stage1_192k[2U * 132U] =
{
  1.1039652163e-03f, 4.2338959873e-02f, 2.3639720678e-01f, 3.2384687662e-01f,
  -7.3128424585e-02f, -1.0591455549e-01f, 1.4070725441e-01f, -1.0142941773e-01f,
  4.2913530022e-02f, 8.1577450037e-03f, -4.2873580009e-02f, 6.1004929245e-02f,
  -6.5728597343e-02f, 6.1013430357e-02f, -5.0489179790e-02f, 3.7059154361e-02f,
  -2.2854639217e-02f, 9.3274693936e-03f, 2.6161931455e-03f, -1.2483903207e-02f,
  2.0082963631e-02f, -2.5428611785e-02f, 2.8672995046e-02f, -3.0051872134e-02f,
  2.9845528305e-02f, -2.8350802138e-02f, 2.5861486793e-02f, -2.2655149922e-02f,
  1.8984591588e-02f, -1.5072863549e-02f, 1.1110742576e-02f, -7.2561404668e-03f,
  3.6347771529e-03f, -3.4189037979e-04f, -2.5554746389e-03f, 5.0156582147e-03f,
  -7.0193791762e-03f, 8.5665173829e-03f, -9.6729230136e-03f, 1.0367212817e-02f,
  -1.0687761940e-02f, 1.0679815896e-02f, -1.0392897762e-02f, 9.8783839494e-03f,
  -9.1874888167e-03f, 8.3694579080e-03f, -7.4701886624e-03f, 6.5310597420e-03f,
  -5.5881706066e-03f, 4.6718223020e-03f, -3.8063174579e-03f, 3.0099719297e-03f,
  -2.2953746375e-03f, 1.6697985120e-03f, -1.1358023621e-03f, 6.9185317261e-04f,
  -3.3311039442e-04f, 5.2164661611e-05f, 1.6018828319e-04f, -3.1426097848e-04f,
  4.2076580576e-04f, -4.9027428031e-04f, 5.3271179786e-04f, -5.5700051598e-04f,
  5.7075568475e-04f, -5.8016681578e-04f, 5.8988603996e-04f, -6.0307327658e-04f,
  6.2149798032e-04f, -6.4569688402e-04f, 6.7516439594e-04f, -7.0863123983e-04f,
  7.4422807666e-04f, -7.7982735820e-04f, 8.1318308366e-04f, -8.4220012650e-04f,
  8.6503283819e-04f, -8.8026374578e-04f, 8.8694877923e-04f, -8.8468135800e-04f,
  8.7356526637e-04f, -8.5420085816e-04f, 8.2758953795e-04f, -7.9507421469e-04f,
  7.5820833445e-04f, -7.1866990766e-04f, 6.7811907502e-04f, -6.3812546432e-04f,
  6.0005509295e-04f, -5.6503049564e-04f, 5.3384091007e-04f, -5.0695223035e-04f,
  4.8447417794e-04f, -4.6620765352e-04f, 4.5165707706e-04f, -4.4010215788e-04f,
  4.3021858437e-04f, -4.3760237168e-04f, 3.8474559551e-04f, -3.7422048626e-04f,
  3.8882435183e-04f, -3.9918418042e-04f, 3.9551855298e-04f, -3.7746128510e-04f,
  3.4789790516e-04f, -3.1039625173e-04f, 2.6821193751e-04f, -2.2402672039e-04f,
  1.7989866319e-04f, -1.3735456741e-04f, 9.7453281342e-05f, -6.0899372329e-05f,
  2.8098596886e-05f, 7.5580771863e-07f, -2.5649231247e-05f, 4.6673634643e-05f,
  -6.4040141297e-05f, 7.7996359323e-05f, -8.8863300334e-05f, 9.6939264040e-05f,
  -1.0257695249e-04f, 1.0609561286e-04f, -1.0781719902e-04f, 1.0804556223e-04f,
  -1.0705249588e-04f, 1.0508635751e-04f, -1.0238085088e-04f, 9.9120981758e-05f,
  -9.5479961601e-05f, 9.1594221885e-05f, -8.7577791419e-05f, 8.3513041318e-05f,
  9.7051346675e-03f, 1.1922133714e-01f, 3.3586013317e-01f, 1.5772244334e-01f,
  -1.9181019068e-01f, 7.3913492262e-02f, 3.0512969941e-02f, -8.4484182298e-02f,
  9.6025273204e-02f, -8.1823378801e-02f, 5.5894061923e-02f, -2.7520367876e-02f,
  1.9692867063e-03f, 1.8289299682e-02f, -3.2534833997e-02f, 4.1044369340e-02f,
  -4.4598676264e-02f, 4.4176969677e-02f, -4.0778625757e-02f, 3.5326629877e-02f,
  -2.8621517122e-02f, 2.1325508133e-02f, -1.3963789679e-02f, 6.9349957630e-03f,
  -5.2592909196e-04f, -5.0722374581e-03f, 9.7480332479e-03f, -1.3454711996e-02f,
  1.6196809709e-02f, -1.8018158153e-02f, 1.8991565332e-02f, -1.9209893420e-02f,
  1.8778560683e-02f, -1.7809210345e-02f, 1.6414565966e-02f, -1.4704197645e-02f,
  1.2781286612e-02f, -1.0740095749e-02f, 8.6642410606e-03f, -6.6255126148e-03f,
  4.6833306551e-03f, -2.8846226633e-03f, 1.2641992653e-03f, 1.5463687305e-04f,
  -1.3590940507e-03f, 2.3457845673e-03f, -3.1193548348e-03f, 3.6911403295e-03f,
  -4.0777074173e-03f, 4.2994627729e-03f, -4.3792845681e-03f, 4.3412921950e-03f,
  -4.2097144760e-03f, 4.0079583414e-03f, -3.7577711046e-03f, 3.4786872566e-03f,
  -3.1875581481e-03f, 2.8983105440e-03f, -2.6218274143e-03f, 2.3660399020e-03f,
  -2.1360525861e-03f, 1.9344438333e-03f, -1.7615916440e-03f, 1.6160971718e-03f,
  -1.4951648191e-03f, 1.3950754656e-03f, -1.3115562033e-03f, 1.2401572894e-03f,
  -1.1765791569e-03f, 1.1169384234e-03f, -1.0579372756e-03f, 9.9705718458e-04f,
  -9.3253189698e-04f, 8.6344807642e-04f, -7.8962289263e-04f, 7.1158132050e-04f,
  -6.3037109794e-04f, 5.4746418027e-04f, -4.6457207645e-04f, 3.8351721014e-04f,
  -3.0606301152e-04f, 2.3381262145e-04f, -1.6807451902e-04f, 1.0981632659e-04f,
  -5.9594487539e-05f, 1.7567464965e-05f, 1.6523619706e-05f, -4.3290321628e-05f,
  6.3663748733e-05f, -7.8782875789e-05f, 8.9950452093e-05f, -9.8512617114e-05f,
  1.0580164235e-04f, -1.1302770872e-04f, 1.2122957560e-04f, -1.3116661285e-04f,
  1.3921687787e-04f, -1.8767500296e-04f, 1.7278852465e-04f, -1.6303340090e-04f,
  1.8064724281e-04f, -2.1545450727e-04f, 2.5607965654e-04f, -2.9496144271e-04f,
  3.2794405706e-04f, -3.5314125125e-04f, 3.7008163054e-04f, -3.7909098319e-04f,
  3.8094224874e-04f, -3.7659873487e-04f, 3.6710192217e-04f, -3.5346651566e-04f,
  3.3665521187e-04f, -3.1753373332e-04f, 2.9688104405e-04f, -2.7534720721e-04f,
  2.5350792566e-04f, -2.3181359575e-04f, 2.1066330373e-04f, -1.9032519776e-04f,
  1.7103239952e-04f, -1.5293031174e-04f, 1.3611429313e-04f, -1.2063710892e-04f,
  1.0650304466e-04f, -9.3678216217e-05f, 8.2121572632e-05f, -7.1753966040e-05f,
  6.2496452301e-05f, -5.4256463045e-05f, 4.6941957407e-05f, -4.0446964704e-05f,
};

static const float spdif_wls_listen_stage2_192k[2U * 16U] =
{
  4.2095731944e-02f, 4.3923145533e-01f, 5.8482125401e-02f, -5.4887566715e-02f,
  1.2911451049e-02f, 7.5509711169e-03f, -6.9928122684e-03f, 3.1540435157e-04f,
  2.6130806655e-03f, -1.6170867020e-03f, 1.3488983677e-04f, 2.7231153217e-04f,
  -7.7422948380e-05f, -4.7667675972e-05f, 5.7282454691e-06f, 1.2263923281e-06f,
  2.1503596008e-01f, 4.0140292048e-01f, -1.5925273299e-01f, 5.5385243148e-02f,
  -1.4097525738e-02f, 9.2803100415e-05f, 2.7309504803e-03f, -1.5891739167e-03f,
  -1.4230849047e-04f, 7.9693587031e-04f, -3.9473397192e-04f, -5.2370385674e-05f,
  8.7817104941e-05f, 9.4174138212e-06f, -4.1053745008e-06f, -9.0175319656e-07f,
};

void SPDIF_WlsListen4x_Reset(SPDIF_WlsListen4x *state)
{
  if (state == NULL) return;
  memset(state->stage1_left_history, 0,
         sizeof(state->stage1_left_history));
  memset(state->stage1_right_history, 0,
         sizeof(state->stage1_right_history));
  memset(state->stage2_left_history, 0,
         sizeof(state->stage2_left_history));
  memset(state->stage2_right_history, 0,
         sizeof(state->stage2_right_history));
  memset(state->noise_error_left, 0, sizeof(state->noise_error_left));
  memset(state->noise_error_right, 0, sizeof(state->noise_error_right));
  state->dither_left = SPDIF_WLS_LISTEN_DITHER_LEFT_SEED;
  state->dither_right = SPDIF_WLS_LISTEN_DITHER_RIGHT_SEED;
  state->stage1_write_index = 0U;
  state->stage2_write_index = 0U;
}

void SPDIF_WlsListen4x_Init(SPDIF_WlsListen4x *state,
                            uint32_t source_rate)
{
  if (state == NULL) return;
  if (source_rate == 44100U)
  {
    state->stage1_coefficients = spdif_wls_listen_stage1_176k4;
    state->stage2_coefficients = spdif_wls_listen_stage2_176k4;
    state->stage1_taps_per_phase = 176U;
    state->input_gain = SPDIF_WLS_LISTEN_INPUT_GAIN_44K1;
  }
  else
  {
    state->stage1_coefficients = spdif_wls_listen_stage1_192k;
    state->stage2_coefficients = spdif_wls_listen_stage2_192k;
    state->stage1_taps_per_phase = 132U;
    state->input_gain = SPDIF_WLS_LISTEN_INPUT_GAIN_48K;
  }
  state->noise_shaping_coefficients =
      SPDIF_NoiseShaper5_GetCoefficients(source_rate * 4U);
  SPDIF_WlsListen4x_Reset(state);
}

uint16_t SPDIF_WlsListen4x_GetTailFrames(
    const SPDIF_WlsListen4x *state)
{
  if (state == NULL) return 0U;
  return (uint16_t)(state->stage1_taps_per_phase +
                    SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS / 2U - 1U);
}

void SPDIF_WlsListen4x_Process(
    SPDIF_WlsListen4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_WLS_LISTEN_FACTOR * 2U])
{
  const uint32_t stage1_taps = state->stage1_taps_per_phase;
  const uint32_t stage1_write = state->stage1_write_index;
  const float left_input = (float)left * state->input_gain;
  const float right_input = (float)right * state->input_gain;
  float stage1_left[2];
  float stage1_right[2];

  state->stage1_left_history[stage1_write] = left_input;
  state->stage1_left_history[stage1_write + stage1_taps] = left_input;
  state->stage1_right_history[stage1_write] = right_input;
  state->stage1_right_history[stage1_write + stage1_taps] = right_input;

  const float *stage1_coefficients0 = state->stage1_coefficients;
  const float *stage1_coefficients1 =
      state->stage1_coefficients + stage1_taps;
  const float *stage1_left_current =
      &state->stage1_left_history[stage1_write + stage1_taps];
  const float *stage1_right_current =
      &state->stage1_right_history[stage1_write + stage1_taps];
  float stage1_left0 = 0.0f;
  float stage1_left1 = 0.0f;
  float stage1_right0 = 0.0f;
  float stage1_right1 = 0.0f;

  for (uint32_t tap = 0U; tap < stage1_taps; tap += 4U)
  {
    for (uint32_t lane = 0U; lane < 4U; ++lane)
    {
      const float left_sample = stage1_left_current[-(int32_t)lane];
      const float right_sample = stage1_right_current[-(int32_t)lane];
      stage1_left0 += stage1_coefficients0[lane] * left_sample;
      stage1_left1 += stage1_coefficients1[lane] * left_sample;
      stage1_right0 += stage1_coefficients0[lane] * right_sample;
      stage1_right1 += stage1_coefficients1[lane] * right_sample;
    }
    stage1_coefficients0 += 4;
    stage1_coefficients1 += 4;
    stage1_left_current -= 4;
    stage1_right_current -= 4;
  }
  stage1_left[0] = stage1_left0;
  stage1_left[1] = stage1_left1;
  stage1_right[0] = stage1_right0;
  stage1_right[1] = stage1_right1;
  state->stage1_write_index =
      (uint16_t)((stage1_write + 1U == stage1_taps) ?
                 0U : stage1_write + 1U);

  for (uint32_t intermediate = 0U; intermediate < 2U; ++intermediate)
  {
    const uint32_t stage2_write = state->stage2_write_index;
    state->stage2_left_history[stage2_write] = stage1_left[intermediate];
    state->stage2_left_history[
        stage2_write + SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS] =
            stage1_left[intermediate];
    state->stage2_right_history[stage2_write] = stage1_right[intermediate];
    state->stage2_right_history[
        stage2_write + SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS] =
            stage1_right[intermediate];

    const float *stage2_coefficients0 = state->stage2_coefficients;
    const float *stage2_coefficients1 =
        state->stage2_coefficients + SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS;
    const float *stage2_left_current =
        &state->stage2_left_history[
            stage2_write + SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS];
    const float *stage2_right_current =
        &state->stage2_right_history[
            stage2_write + SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS];
    float stage2_left0 = 0.0f;
    float stage2_left1 = 0.0f;
    float stage2_right0 = 0.0f;
    float stage2_right1 = 0.0f;

    for (uint32_t tap = 0U;
         tap < SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS; tap += 4U)
    {
      for (uint32_t lane = 0U; lane < 4U; ++lane)
      {
        const float left_sample = stage2_left_current[-(int32_t)lane];
        const float right_sample = stage2_right_current[-(int32_t)lane];
        stage2_left0 += stage2_coefficients0[lane] * left_sample;
        stage2_left1 += stage2_coefficients1[lane] * left_sample;
        stage2_right0 += stage2_coefficients0[lane] * right_sample;
        stage2_right1 += stage2_coefficients1[lane] * right_sample;
      }
      stage2_coefficients0 += 4;
      stage2_coefficients1 += 4;
      stage2_left_current -= 4;
      stage2_right_current -= 4;
    }

    const uint32_t output_phase = intermediate * 2U;
    output[output_phase * 2U] = SPDIF_NoiseShaper5_Quantize(
        stage2_left0, &state->dither_left, state->noise_error_left,
        state->noise_shaping_coefficients);
    output[output_phase * 2U + 1U] = SPDIF_NoiseShaper5_Quantize(
        stage2_right0, &state->dither_right, state->noise_error_right,
        state->noise_shaping_coefficients);
    output[(output_phase + 1U) * 2U] = SPDIF_NoiseShaper5_Quantize(
        stage2_left1, &state->dither_left, state->noise_error_left,
        state->noise_shaping_coefficients);
    output[(output_phase + 1U) * 2U + 1U] =
        SPDIF_NoiseShaper5_Quantize(
            stage2_right1, &state->dither_right,
            state->noise_error_right, state->noise_shaping_coefficients);

    state->stage2_write_index =
        (uint8_t)((stage2_write + 1U ==
                   SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS) ?
                  0U : stage2_write + 1U);
  }
}
