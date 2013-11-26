
#include <Python.h>
#include <structmember.h>

#include <iostream>
#include <stdio.h>
#include <string>
#include "config/config.hpp"
#include "lem_interface/lem_interface.hpp"
#include "qclassify/qclassify.hpp"
#include "qclassify/qclassify_impl.hpp"
#include "qclassify/htmlmark.hpp"

using namespace gogo;

#define ESTATUS_OK          0
#define ESTATUS_CONFERROR   1
#define ESTATUS_INITERROR   2
#define ESTATUS_INDEXERROR  3
#define ESTATUS_LOADERROR   4

typedef struct {
    const PhraseSearcher *m_psrch;
    PhraseCollectionLoader m_ldr;
    static lemInterface *m_pLem;
    static int lem_nrefs;
    XmlConfig m_cfg;
    std::string m_req;
    QCHtmlMarker m_marker;
    PhraseSearcher::res_t m_clsRes;
} CAgent;


lemInterface *CAgent::m_pLem = NULL;
int CAgent::lem_nrefs = 0;

CAgent cagent;

PyObject* pyagent_instance = NULL;

PyObject* PyExc_QClassifyError = PyErr_NewException((char *)"pyQClassify.QClassifyError", NULL, NULL);


int prepareSearch(PyObject* self);
int loadConfig(PyObject* self, const char *path) ;

 /***********************************************************************************************/
 /*                                    CORE DEFENITION                                          */
 /***********************************************************************************************/

static void PyAgent_dealloc(PyObject* self)
{
    if (--(cagent.lem_nrefs) == 0) 
    {
        delete cagent.m_pLem;
        cagent.m_pLem = NULL;
    }  

    self->ob_type->tp_free((PyObject*)self);
}

static PyObject * PyAgent_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *self;

    if (pyagent_instance != NULL){
        Py_INCREF(pyagent_instance);
        return pyagent_instance;
    }

    self = (PyObject *)type->tp_alloc(type, 0);
    pyagent_instance = self;
    return (PyObject *)self;
}

static int PyAgent_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    char *path=NULL;
    int error;

    static char *kwlist[] = {(char *)"path", NULL};

    PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &path);        

    cagent.m_psrch = 0;

    try 
    {
        if (!cagent.m_pLem )
        {
            assert(!cagent.lem_nrefs);
            cagent.m_pLem = new lemInterface();
        }
        cagent.lem_nrefs++;
    } 
    catch(...) 
    {
        PyErr_SetString(PyExc_RuntimeError, "Error occured while initializing lemmatizer libraty");
        return -1;
    }

    if (path != NULL)
    {
        error = loadConfig(self, path);

        if (error != ESTATUS_OK)
        {
            PyErr_SetString(PyExc_RuntimeError, "Error occured while loading configuration file");
            return -1;
        };
    }

    return 0;
}


static PyMemberDef PyAgent_members[] = {
    { NULL, 0, 0, 0, NULL }  /* Sentinel */
};

 /***********************************************************************************************/
 /*                                        C METHODS                                            */
 /***********************************************************************************************/
 
int loadConfig(PyObject* self, const char *path) 
{
    cagent.m_psrch = NULL;
    
    try 
    {
        if (!cagent.m_cfg.Load(path)) 
            return ESTATUS_CONFERROR;
    } 
    catch(std::exception& ex)
    {
        return ESTATUS_CONFERROR;
    }
    catch(...) 
    {
        return ESTATUS_CONFERROR;
    }
    return ESTATUS_OK;
}

int index2file(PyObject* self) 
{
    try 
    {
       PhraseCollectionIndexer idx(cagent.m_pLem); 
       idx.indexByConfig( &(cagent.m_cfg) );
       idx.save();
    } 
    catch(...) 
    {
        return ESTATUS_INDEXERROR;
    }
    return ESTATUS_OK;
}

// PhraseSearcher::res_t& searchPhrase(PyObject* self, const char *s) 
// {
//     if (!cagent.m_psrch) 
//       prepareSearch(self);

//     cagent.m_req.assign(s);
//     cagent.m_psrch->searchPhrase(cagent.m_req, cagent.m_clsRes);
//     return cagent.m_clsRes;
// }

int initMarkup(PyObject* self) 
{
    int ret = prepareSearch(self);
    if (ret != ESTATUS_OK)
        return ret;

    cagent.m_marker.setPhraseSearcher(cagent.m_psrch);
    cagent.m_marker.loadSettings( &cagent.m_cfg );
    return ESTATUS_OK;
}

unsigned callMarkup(PyObject* self, 
                    const std::string &s, std::string &out, 
                    const QCHtmlMarker::MarkupSettings &st) 
{
    return cagent.m_marker.markup(s, out, st);
}


void getMarkupSettings(PyObject* self, QCHtmlMarker::MarkupSettings *pst) 
{
    *pst = cagent.m_marker.getConfigSettings();
}

void getIndexFileName(PyObject* self, std::string &s) 
{
    cagent.m_cfg.GetStr("QueryQualifier", "IndexFile", s, "phrases.idx");
}

int prepareSearch(PyObject* self) 
{
    cagent.m_ldr.setLemmatizer(cagent.m_pLem);

    if (!cagent.m_ldr.loadByConfig( &cagent.m_cfg ) ) 
    {
        return ESTATUS_LOADERROR;
    }   

    cagent.m_psrch = cagent.m_ldr.getSearcher();
    return ESTATUS_OK;
}

/***********************************************************************************************/
/*                                      WRAPPER METHODS                                        */
/***********************************************************************************************/

static PyObject * PyAgent_initMarkup(PyObject* self)
{
    if (initMarkup(self)) {
        PyErr_SetString(PyExc_QClassifyError, "Unable to init markup.");
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject * PyAgent_reinitMarkup(PyObject* self)
{
    if (initMarkup(self)) {
        PyErr_SetString(PyExc_QClassifyError, "Unable to reinit markup.");
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* PyAgent_version(PyObject* self)
{
    return PyInt_FromLong(qcls_impl::QCLASSIFY_INDEX_VERSION);
}

static PyObject* PyAgent_loadConfig(PyObject* self, PyObject *args, PyObject *kwds)
{
    char* path=NULL;

    static char *kwlist[] = {(char *)"path", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &path))
        return NULL; 

    if (loadConfig(self, path)) {
        PyErr_SetString(PyExc_QClassifyError, "Unable to load config.");
        return NULL;
    }
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* PyAgent_index2file(PyObject* self)
{
    if (index2file(self)) {
        PyErr_SetString(PyExc_QClassifyError, "Error while call index2file.");
        return NULL;
    }
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* PyAgent_getIndexFileName(PyObject* self)
{
    PyObject* result;
    std::string s;
    getIndexFileName(self, s);

    result = PyString_FromString(s.c_str());

    Py_INCREF(result);
    return result;
}

// static PyObject* PyAgent_classifyPhrase(PyObject* self, PyObject *args, PyObject *kwds)
// {
//     char* phrase=NULL;
//     PyObject* res_list = PyList_New(0);
//     PyObject* res_item;

//     static char *kwlist[] = {(char *)"phrase", NULL};

//     if (! PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &phrase))
//         return NULL;

//     PhraseSearcher::res_t &r = searchPhrase(self, phrase);
//     if (r.size() != 0) 
//     {
//         for(PhraseSearcher::res_t::const_iterator it = r.begin(); it != r.end(); it++) 
//         {
//             res_item = PyTuple_Pack(2, PyString_FromString(it->first.c_str()), PyLong_FromLong(it->second) );
//             PyList_Append(res_list, res_item);
//         }

//         Py_INCREF(res_list);
//         return res_list;
//     }
//     Py_INCREF(Py_None);
//     return Py_None;
// }

static PyObject* PyAgent_markup(PyObject* self, PyObject *args, PyObject *kwds)
{
    QCHtmlMarker::MarkupSettings st;
    std::string out;

    char* text=NULL;

    static char *kwlist[] = {(char *)"text", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &text))
        return NULL;

    getMarkupSettings(self, &st);
    callMarkup(self, (std::string)text, out, st);

    return PyString_FromString(out.c_str());

}


static PyMethodDef PyAgent_methods[] = 
{
    {"initMarkup", (PyCFunction)PyAgent_initMarkup, METH_NOARGS, ""},
    {"reinitMarkup", (PyCFunction)PyAgent_reinitMarkup, METH_NOARGS, ""},
    {"version", (PyCFunction)PyAgent_version, METH_NOARGS, "C library version"},    
    {"loadConfig", (PyCFunction)PyAgent_loadConfig, METH_KEYWORDS, ""},
    {"index2file", (PyCFunction)PyAgent_index2file, METH_NOARGS, ""},
    {"getIndexFileName", (PyCFunction)PyAgent_getIndexFileName, METH_NOARGS, ""},
    // {"classifyPhrase", (PyCFunction)PyAgent_classifyPhrase, METH_KEYWORDS, ""},
    {"markup", (PyCFunction)PyAgent_markup, METH_KEYWORDS, ""},
    {NULL,  NULL, 0, NULL }  /* Sentinel */
};

static PyTypeObject PyAgentType = {
    PyObject_HEAD_INIT(NULL)
    0,                              /*ob_size*/
    "pyQClassify.Agent",   /*tp_name*/
    sizeof(PyObject),         /*tp_basicsize*/
    0,                                  /*tp_itemsize*/
    (destructor)PyAgent_dealloc, /*tp_dealloc*/
    0,                                  /*tp_print*/
    0,                                  /*tp_getattr*/
    0,                                  /*tp_setattr*/
    0,                                  /*tp_compare*/
    0,                                  /*tp_repr*/
    0,                                  /*tp_as_number*/
    0,                                  /*tp_as_sequence*/
    0,                                  /*tp_as_mapping*/
    0,                                  /*tp_hash */
    0,                                  /*tp_call*/
    0,                                  /*tp_str*/
    0,                                  /*tp_getattro*/
    0,                                  /*tp_setattro*/
    0,                                  /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    "Singleton Agent class",            /* tp_doc */
    0,                                  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    PyAgent_methods,             /* tp_methods */
    PyAgent_members,             /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    (initproc)PyAgent_init,      /* tp_init */
    0,                                  /* tp_alloc */
    PyAgent_new,                 /* tp_new */
    0, /* tp_free */
    0, /* tp_is_gc */
    0, /* tp_bases */
    0, /* tp_mro */
    0, /* tp_cache */
    0, /* tp_subclasses */
    0, /* tp_weaklist */
    0, /* tp_del */
    0, /* tp_version_tag */
};

static PyMethodDef module_functions[] = {
    { NULL, NULL, 0, NULL }
};

#ifndef PyMODINIT_FUNC  /* declarations for DLL import/export */
    #define PyMODINIT_FUNC void
#endif

PyMODINIT_FUNC initpyQClassify(void) 
{
    PyObject* module;

    if (PyType_Ready(&PyAgentType) < 0)
        return;

    module = Py_InitModule3("pyQClassify", module_functions, "Python wrapper around QClassify library.");

    if (module == NULL)
      return;


    PyModule_AddObject(module, "QClassifyError", PyExc_QClassifyError);  

    Py_INCREF((PyObject*)&PyAgentType);
    PyModule_AddObject(module, "Agent", (PyObject *)&PyAgentType);
}