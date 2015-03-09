#include <iostream>
#include <stdexcept>

#include "ndds/ndds_cpp.h"
#include "ndds/ndds_requestreply_cpp.h"

#include <rmw/rmw.h>
#include <rmw/allocators.h>
#include <rmw/error_handling.h>
#include <rmw/types.h>

#include "rosidl_generator_cpp/MessageTypeSupport.h"
#include "rosidl_typesupport_connext_cpp/MessageTypeSupport.h"

#include "rosidl_generator_cpp/ServiceTypeSupport.h"
#include "rosidl_typesupport_connext_cpp/ServiceTypeSupport.h"

extern "C"
{

const char * _rti_connext_identifier = "connext_static";

const char *
rmw_get_implementation_identifier() {
  return _rti_connext_identifier;
}

struct CustomServiceInfo {
  void * replier_;
  DDSDataReader * request_datareader_;
  rmw_connext_cpp::ServiceTypeSupportCallbacks * callbacks_;
};

struct CustomClientInfo {
  void * requester_;
  DDSDataReader * response_datareader_;
  rmw_connext_cpp::ServiceTypeSupportCallbacks * callbacks_;
};

rmw_ret_t
rmw_init()
{
    DDSDomainParticipantFactory* dpf_ = DDSDomainParticipantFactory::get_instance();
    if (!dpf_) {
        rmw_set_error_string("  init() could not get participant factory");
        return RMW_RET_ERROR;
    };
    return RMW_RET_OK;
}

rmw_node_t *
rmw_create_node(const char * name)
{
    DDSDomainParticipantFactory* dpf_ = DDSDomainParticipantFactory::get_instance();
    if (!dpf_) {
        rmw_set_error_string("  create_node() could not get participant factory");
        return NULL;
    };

    // use loopback interface to enable cross vendor communication
    DDS_DomainParticipantQos participant_qos;
    DDS_ReturnCode_t status = dpf_->get_default_participant_qos(participant_qos);
    if (status != DDS_RETCODE_OK)
    {
        rmw_set_error_string("  create_node() could not get participant qos");
        return NULL;
    }
    status = DDSPropertyQosPolicyHelper::add_property(participant_qos.property,
        "dds.transport.UDPv4.builtin.ignore_loopback_interface",
        "0",
        DDS_BOOLEAN_FALSE);
    if (status != DDS_RETCODE_OK)
    {
        rmw_set_error_string("  create_node() could not add qos property");
        return NULL;
    }

    DDS_DomainId_t domain = 0;

    DDSDomainParticipant* participant = dpf_->create_participant(
        //domain, DDS_PARTICIPANT_QOS_DEFAULT, NULL,
        domain, participant_qos, NULL,
        DDS_STATUS_MASK_NONE);
    if (!participant) {
        rmw_set_error_string("  create_node() could not create participant");
        return NULL;
    };

    rmw_node_t * node_handle = new rmw_node_t;
    node_handle->implementation_identifier = _rti_connext_identifier;
    node_handle->data = participant;

    return node_handle;
}

struct CustomPublisherInfo {
  DDSDataWriter * topic_writer_;
  rmw_connext_cpp::MessageTypeSupportCallbacks * callbacks_;
};

rmw_publisher_t *
rmw_create_publisher(const rmw_node_t * node,
                     const rosidl_message_type_support_t * type_support,
                     const char * topic_name,
                     size_t queue_size)
{
    if (node->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("node handle not from this implementation");
        // printf("but from: %s\n", node->implementation_identifier);
        return NULL;
    }

    DDSDomainParticipant* participant = (DDSDomainParticipant*)node->data;

    rmw_connext_cpp::MessageTypeSupportCallbacks * callbacks = (rmw_connext_cpp::MessageTypeSupportCallbacks*)type_support->data;
    std::string type_name = std::string(callbacks->_package_name) + "::dds_::" + callbacks->_message_name + "_";

    callbacks->_register_type(participant, type_name.c_str());


    DDS_PublisherQos publisher_qos;
    DDS_ReturnCode_t status = participant->get_default_publisher_qos(publisher_qos);
    if (status != DDS_RETCODE_OK) {
        rmw_set_error_string("failed to get default publisher qos");
        // printf("get_default_publisher_qos() failed. Status = %d\n", status);
        return NULL;
    };

    DDSPublisher* dds_publisher = participant->create_publisher(
        publisher_qos, NULL, DDS_STATUS_MASK_NONE);
    if (!dds_publisher) {
        rmw_set_error_string("  create_publisher() could not create publisher");
        return NULL;
    };


    DDS_TopicQos default_topic_qos;
    status = participant->get_default_topic_qos(default_topic_qos);
    if (status != DDS_RETCODE_OK) {
        rmw_set_error_string("failed to get default topic qos");
        // printf("get_default_topic_qos() failed. Status = %d\n", status);
        return NULL;
    };

    DDSTopic* topic = participant->create_topic(
        topic_name, type_name.c_str(), default_topic_qos, NULL,
        DDS_STATUS_MASK_NONE
    );
    if (!topic) {
        rmw_set_error_string("  create_topic() could not create topic");
        return NULL;
    };


    DDS_DataWriterQos datawriter_qos;
    status = participant->get_default_datawriter_qos(datawriter_qos);
    if (status != DDS_RETCODE_OK) {
        rmw_set_error_string("failed to get default datawriter qos");
        // printf("get_default_datawriter_qos() failed. Status = %d\n", status);
        return NULL;
    };

    status = DDSPropertyQosPolicyHelper::add_property(datawriter_qos.property,
        "dds.data_writer.history.memory_manager.fast_pool.pool_buffer_max_size",
        "4096",
        DDS_BOOLEAN_FALSE);
    if (status != DDS_RETCODE_OK)
    {
        rmw_set_error_string("  add_property() could not add qos property");
        return NULL;
    }

    DDSDataWriter* topic_writer = dds_publisher->create_datawriter(
        topic, datawriter_qos,
        NULL, DDS_STATUS_MASK_NONE);


    CustomPublisherInfo* custom_publisher_info = new CustomPublisherInfo();
    custom_publisher_info->topic_writer_ = topic_writer;
    custom_publisher_info->callbacks_ = callbacks;

    rmw_publisher_t * publisher = new rmw_publisher_t;
    publisher->implementation_identifier = _rti_connext_identifier;
    publisher->data = custom_publisher_info;
    return publisher;
}

rmw_ret_t
rmw_publish(const rmw_publisher_t * publisher, const void * ros_message)
{
    if (publisher->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("publisher handle not from this implementation");
        // rmw_set_error_string("but from: %s\n", publisher->implementation_identifier);
        return RMW_RET_ERROR;
    }

    CustomPublisherInfo * custom_publisher_info = (CustomPublisherInfo*)publisher->data;
    DDSDataWriter * topic_writer = custom_publisher_info->topic_writer_;
    const rmw_connext_cpp::MessageTypeSupportCallbacks * callbacks = custom_publisher_info->callbacks_;

    callbacks->_publish(topic_writer, ros_message);
    return RMW_RET_OK;
}

struct CustomSubscriberInfo {
  DDSDataReader * topic_reader_;
  rmw_connext_cpp::MessageTypeSupportCallbacks * callbacks_;
};

rmw_subscription_t *
rmw_create_subscription(const rmw_node_t * node,
                        const rosidl_message_type_support_t * type_support,
                        const char * topic_name,
                        size_t queue_size)
{
    if (node->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("node handle not from this implementation");
        // printf("but from: %s\n", node->implementation_identifier);
        return NULL;
    }

    DDSDomainParticipant* participant = (DDSDomainParticipant*)node->data;

    rmw_connext_cpp::MessageTypeSupportCallbacks * callbacks = (rmw_connext_cpp::MessageTypeSupportCallbacks*)type_support->data;
    std::string type_name = std::string(callbacks->_package_name) + "::dds_::" + callbacks->_message_name + "_";

    callbacks->_register_type(participant, type_name.c_str());

    DDS_SubscriberQos subscriber_qos;
    DDS_ReturnCode_t status = participant->get_default_subscriber_qos(subscriber_qos);
    if (status != DDS_RETCODE_OK) {
        rmw_set_error_string("failed to get default subscriber qos");
        // printf("get_default_subscriber_qos() failed. Status = %d\n", status);
        return NULL;
    };

    DDSSubscriber* dds_subscriber = participant->create_subscriber(
        subscriber_qos, NULL, DDS_STATUS_MASK_NONE);
    if (!dds_subscriber) {
        rmw_set_error_string("  create_subscriber() could not create subscriber");
        return NULL;
    };

    DDS_TopicQos default_topic_qos;
    status = participant->get_default_topic_qos(default_topic_qos);
    if (status != DDS_RETCODE_OK) {
        rmw_set_error_string("failed to get default topic qos");
        // printf("get_default_topic_qos() failed. Status = %d\n", status);
        return NULL;
    };

    DDSTopic* topic = participant->create_topic(
        topic_name, type_name.c_str(), default_topic_qos, NULL,
        DDS_STATUS_MASK_NONE
    );
    if (!topic) {
        rmw_set_error_string("  create_topic() could not create topic");
        return NULL;
    };

    DDS_DataReaderQos default_datareader_qos;
    status = participant->get_default_datareader_qos(default_datareader_qos);
    if (status != DDS_RETCODE_OK) {
        rmw_set_error_string("failed to get default datareader qos");
        // printf("get_default_datareader_qos() failed. Status = %d\n", status);
        return NULL;
    };

    DDSDataReader* topic_reader = dds_subscriber->create_datareader(
        topic, default_datareader_qos,
        NULL, DDS_STATUS_MASK_NONE);


    CustomSubscriberInfo* custom_subscriber_info = new CustomSubscriberInfo();
    custom_subscriber_info->topic_reader_ = topic_reader;
    custom_subscriber_info->callbacks_ = callbacks;

    rmw_subscription_t * subscription = new rmw_subscription_t;
    subscription->implementation_identifier = _rti_connext_identifier;
    subscription->data = custom_subscriber_info;
    return subscription;
}

rmw_ret_t
rmw_take(const rmw_subscription_t * subscription, void * ros_message, bool * taken)
{
    if (taken == NULL) {
        rmw_set_error_string("taken argument can't be null");
        return RMW_RET_ERROR;
    }

    if (subscription->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("subscriber handle not from this implementation");
        // printf("but from: %s\n", subscription->implementation_identifier);
        return RMW_RET_ERROR;
    }

    CustomSubscriberInfo * custom_subscriber_info = (CustomSubscriberInfo*)subscription->data;
    DDSDataReader* topic_reader = custom_subscriber_info->topic_reader_;
    const rmw_connext_cpp::MessageTypeSupportCallbacks * callbacks = custom_subscriber_info->callbacks_;

    *taken = callbacks->_take(topic_reader, ros_message);

    return RMW_RET_OK;
}

rmw_guard_condition_t *
rmw_create_guard_condition()
{
    rmw_guard_condition_t * guard_condition = new rmw_guard_condition_t;
    guard_condition->implementation_identifier = _rti_connext_identifier;
    guard_condition->data = new DDSGuardCondition();
    return guard_condition;

}

rmw_ret_t
rmw_destroy_guard_condition(rmw_guard_condition_t * guard_condition)
{
  if (guard_condition) {
    delete guard_condition->data;
    delete guard_condition;
    return RMW_RET_OK;
  }

  return RMW_RET_ERROR;
}

rmw_ret_t
rmw_trigger_guard_condition(const rmw_guard_condition_t * guard_condition_handle)
{
    if (guard_condition_handle->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("guard condition handle not from this implementation");
        // printf("but from: %s\n", guard_condition_handle->implementation_identifier);
        return RMW_RET_ERROR;
    }

    DDSGuardCondition * guard_condition = (DDSGuardCondition*)guard_condition_handle->data;
    guard_condition->set_trigger_value(DDS_BOOLEAN_TRUE);
    return RMW_RET_OK;
}

rmw_ret_t
rmw_wait(rmw_subscriptions_t * subscriptions,
         rmw_guard_conditions_t * guard_conditions,
         rmw_services_t * services,
         rmw_clients_t * clients,
         bool non_blocking)
{
    DDSWaitSet waitset;

    // add a condition for each subscriber
    for (unsigned long i = 0; i < subscriptions->subscriber_count; ++i)
    {
        void * data = subscriptions->subscribers[i];
        CustomSubscriberInfo * custom_subscriber_info = (CustomSubscriberInfo*)data;
        DDSDataReader* topic_reader = custom_subscriber_info->topic_reader_;
        DDSStatusCondition * condition = topic_reader->get_statuscondition();
        condition->set_enabled_statuses(DDS_DATA_AVAILABLE_STATUS);
        waitset.attach_condition(condition);
    }

    // add a condition for each guard condition
    for (unsigned long i = 0; i < guard_conditions->guard_condition_count; ++i)
    {
        void * data = guard_conditions->guard_conditions[i];
        DDSGuardCondition * guard_condition = (DDSGuardCondition*)data;
        waitset.attach_condition(guard_condition);
    }

    // add a condition for each service
    for (unsigned long i = 0; i < services->service_count; ++i)
    {
        void * data = services->services[i];
        CustomServiceInfo * custom_service_info = (CustomServiceInfo*)data;
        DDSDataReader* request_datareader = custom_service_info->request_datareader_;
        DDSStatusCondition * condition = request_datareader->get_statuscondition();
        condition->set_enabled_statuses(DDS_DATA_AVAILABLE_STATUS);

        waitset.attach_condition(condition);
    }

    // add a condition for each client
    for (unsigned long i = 0; i < clients->client_count; ++i)
    {
        void * data = clients->clients[i];
        CustomClientInfo * custom_client_info = (CustomClientInfo*)data;
        DDSDataReader* response_datareader = custom_client_info->response_datareader_;
        DDSStatusCondition * condition = response_datareader->get_statuscondition();
        condition->set_enabled_statuses(DDS_DATA_AVAILABLE_STATUS);

        waitset.attach_condition(condition);
    }

    // invoke wait until one of the conditions triggers
    DDSConditionSeq active_conditions;
    DDS_Duration_t timeout = DDS_Duration_t::from_seconds(non_blocking ? 0 : 1);
    DDS_ReturnCode_t status = DDS_RETCODE_TIMEOUT;
    while (DDS_RETCODE_TIMEOUT == status)
    {
        status = waitset.wait(active_conditions, timeout);
        if (DDS_RETCODE_TIMEOUT == status) {
            if (non_blocking)
            {
               break;
            }
            continue;
        };
        if (status != DDS_RETCODE_OK) {
            rmw_set_error_string("failed to wait on waitset");
            // printf("wait() failed. Status = %d\n", status);
            return RMW_RET_ERROR;
        };
    }

    // set subscriber handles to zero for all not triggered conditions
    for (unsigned long i = 0; i < subscriptions->subscriber_count; ++i)
    {
        void * data = subscriptions->subscribers[i];
        CustomSubscriberInfo * custom_subscriber_info = (CustomSubscriberInfo*)data;
        DDSDataReader* topic_reader = custom_subscriber_info->topic_reader_;
        DDSStatusCondition * condition = topic_reader->get_statuscondition();

        // search for subscriber condition in active set
        unsigned long j = 0;
        for (; j < active_conditions.length(); ++j)
        {
            if (active_conditions[j] == condition)
            {
                break;
            }
        }
        // if subscriber condition is not found in the active set
        // reset the subscriber handle
        if (!(j < active_conditions.length()))
        {
            subscriptions->subscribers[i] = 0;
        }
    }

    // set subscriber handles to zero for all not triggered conditions
    for (unsigned long i = 0; i < guard_conditions->guard_condition_count; ++i)
    {
        void * data = guard_conditions->guard_conditions[i];
        DDSCondition * condition = (DDSCondition*)data;

        // search for guard condition in active set
        unsigned long j = 0;
        for (; j < active_conditions.length(); ++j)
        {
            if (active_conditions[j] == condition)
            {
                DDSGuardCondition *guard = (DDSGuardCondition*)condition;
                guard->set_trigger_value(DDS_BOOLEAN_FALSE);
                break;
            }
        }
        // if guard condition is not found in the active set
        // reset the guard handle
        if (!(j < active_conditions.length()))
        {
            guard_conditions->guard_conditions[i] = 0;
        }
    }

    // set service handles to zero for all not triggered conditions
    for (unsigned long i = 0; i < services->service_count; ++i)
    {
        void * data = services->services[i];
        CustomServiceInfo * custom_service_info = (CustomServiceInfo*)data;
        DDSDataReader* request_datareader = custom_service_info->request_datareader_;
        DDSStatusCondition * condition = request_datareader->get_statuscondition();

        // search for service condition in active set
        unsigned long j = 0;
        for (; j < active_conditions.length(); ++j)
        {
            if (active_conditions[j] == condition)
            {
                break;
            }
        }
        // if service condition is not found in the active set
        // reset the subscriber handle
        if (!(j < active_conditions.length()))
        {
            services->services[i] = 0;
        }
    }

    // set client handles to zero for all not triggered conditions
    for (unsigned long i = 0; i < clients->client_count; ++i)
    {
        void * data = clients->clients[i];
        CustomClientInfo * custom_client_info = (CustomClientInfo*)data;
        DDSDataReader* response_datareader = custom_client_info->response_datareader_;
        DDSStatusCondition * condition = response_datareader->get_statuscondition();

        // search for service condition in active set
        unsigned long j = 0;
        for (; j < active_conditions.length(); ++j)
        {
            if (active_conditions[j] == condition)
            {
                break;
            }
        }
        // if client condition is not found in the active set
        // reset the subscriber handle
        if (!(j < active_conditions.length()))
        {
            clients->clients[i] = 0;
        }
    }
    return RMW_RET_OK;
}

rmw_client_t *
rmw_create_client(
  const rmw_node_t * node,
  const rosidl_service_type_support_t * type_support,
  const char * service_name)
{
    if (node->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("node handle not from this implementation");
        // printf("but from: %s\n", node->implementation_identifier);
        return NULL;
    }

    DDSDomainParticipant* participant = (DDSDomainParticipant*)node->data;

    rmw_connext_cpp::ServiceTypeSupportCallbacks * callbacks = (rmw_connext_cpp::ServiceTypeSupportCallbacks*)type_support->data;

    DDSDataReader * response_datareader;

    void * requester = callbacks->_create_requester(participant, service_name, &response_datareader);

    CustomClientInfo* custom_client_info = new CustomClientInfo();
    custom_client_info->requester_ = requester;
    custom_client_info->callbacks_ = callbacks;
    custom_client_info->response_datareader_ = response_datareader;

    rmw_client_t * client = new rmw_client_t;
    client->implementation_identifier =_rti_connext_identifier;
    client->data = custom_client_info;
    return client;
}

rmw_ret_t
rmw_destroy_client(rmw_client_t * client)
{
  if (client) {
    // TODO(esteve): de-allocate Requester and response DataReader
    delete client->data;
    delete client;
    return RMW_RET_OK;
  }

  return RMW_RET_ERROR;
}

rmw_ret_t
rmw_send_request(const rmw_client_t * client, const void * ros_request,
                 int64_t * sequence_id)
{
    if (client->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("client handle not from this implementation");
        // printf("but from: %s\n", client->implementation_identifier);
        return RMW_RET_ERROR;
    }

    CustomClientInfo * custom_client_info = (CustomClientInfo*)client->data;
    void * requester = custom_client_info->requester_;
    const rmw_connext_cpp::ServiceTypeSupportCallbacks * callbacks = custom_client_info->callbacks_;

    *sequence_id = callbacks->_send_request(requester, ros_request);
    return RMW_RET_OK;
}

rmw_service_t *
rmw_create_service(const rmw_node_t * node,
                   const rosidl_service_type_support_t * type_support,
                   const char * service_name)
{
    if (node->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("node handle not from this implementation");
        // printf("but from: %s\n", node->implementation_identifier);
        return NULL;
    }

    DDSDomainParticipant* participant = (DDSDomainParticipant*)node->data;

    rmw_connext_cpp::ServiceTypeSupportCallbacks * callbacks = (rmw_connext_cpp::ServiceTypeSupportCallbacks*)type_support->data;

    DDSDataReader * request_datareader;

    void * replier = callbacks->_create_replier(participant, service_name, &request_datareader);

    CustomServiceInfo* custom_service_info = new CustomServiceInfo();
    custom_service_info->replier_ = replier;
    custom_service_info->callbacks_ = callbacks;
    custom_service_info->request_datareader_ = request_datareader;

    rmw_service_t * service = new rmw_service_t;
    service->implementation_identifier =_rti_connext_identifier;
    service->data = custom_service_info;
    return service;
}

rmw_ret_t
rmw_destroy_service(rmw_service_t * service)
{
  if(service) {
    delete service->data;
    delete service;
    return RMW_RET_OK;
  }

  return RMW_RET_ERROR;
}

rmw_ret_t
rmw_take_request(const rmw_service_t * service,
                 void * ros_request_header, void * ros_request, bool * taken)
{
    if (taken == NULL) {
        rmw_set_error_string("taken argument can't be null");
        return RMW_RET_ERROR;
    }

    if (service->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("service handle not from this implementation");
        // printf("but from: %s\n", service->implementation_identifier);
        return RMW_RET_ERROR;
    }

    CustomServiceInfo * custom_service_info = (CustomServiceInfo*)service->data;

    void * replier = custom_service_info->replier_;

    const rmw_connext_cpp::ServiceTypeSupportCallbacks * callbacks = custom_service_info->callbacks_;

    *taken = callbacks->_take_request(replier, ros_request_header, ros_request);

    return RMW_RET_OK;
}

rmw_ret_t
rmw_take_response(const rmw_client_t * client,
                  void * ros_request_header, void * ros_response, bool * taken)
{
    if (taken == NULL) {
        rmw_set_error_string("taken argument can't be null");
        return RMW_RET_ERROR;
    }

    if (client->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("client handle not from this implementation");
        // printf("but from: %s\n", client->implementation_identifier);
        return RMW_RET_ERROR;
    }

    CustomClientInfo * custom_client_info = (CustomClientInfo*)client->data;

    void * requester = custom_client_info->requester_;

    const rmw_connext_cpp::ServiceTypeSupportCallbacks * callbacks = custom_client_info->callbacks_;

    *taken = callbacks->_take_response(requester, ros_request_header, ros_response);

    return RMW_RET_OK;
}

rmw_ret_t
rmw_send_response(const rmw_service_t * service,
                  void * ros_request_header, void * ros_response)
{
    if (service->implementation_identifier != _rti_connext_identifier)
    {
        rmw_set_error_string("service handle not from this implementation");
        // printf("but from: %s\n", service->implementation_identifier);
        return RMW_RET_ERROR;
    }

    CustomServiceInfo * custom_service_info = (CustomServiceInfo*)service->data;

    void * replier = custom_service_info->replier_;

    const rmw_connext_cpp::ServiceTypeSupportCallbacks * callbacks = custom_service_info->callbacks_;

    callbacks->_send_response(replier, ros_request_header, ros_response);
    return RMW_RET_OK;
}

}